// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "DecryptedDownload.h"

#ifdef BUILD_WITH_DECRYPTED_DOWNLOAD

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <memory>
#include <stdlib.h>
#include <string.h>

#include "Aegis.h"
#include "AsyncFsManager.h"
#include "AudioFileRecorder.h"
#include "WebServer.h"
#include "WifiManager.h"
#include "dbg_log.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include "mbedtls/gcm.h"
#include "mbedtls/platform_util.h"
#include "utilfuncs.h"

namespace DecryptedDownload
{
namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr size_t      kFileNameBufferSize = 256;
constexpr const char* TAG                 = "DecryptedDownload";
constexpr const char* kErrorAttribute     = "decrypted_download_error";
constexpr const char* kStatusAttribute    = "decrypted_download_status";
constexpr const char* kStartedAttribute   = "decrypted_download_started";

#if BUILD_WITH_SECURITY_LEVEL >= 1 && defined(BUILD_WITH_ENCRYPTED_PLAYBACK)
constexpr size_t   kGcmNonceSize               = 12;
constexpr size_t   kGcmTagSize                 = 16;
constexpr uint8_t  kEncryptedRequestMagic[]    = {'T', 'F', 'G', 'C'};
constexpr uint8_t  kEncryptedRequestVersion    = 1;
constexpr size_t   kEncryptedRequestHeaderSize = sizeof(kEncryptedRequestMagic) + 1 + kGcmNonceSize;
constexpr size_t   kEncryptedRequestMinSize    = kEncryptedRequestHeaderSize + kGcmTagSize;
constexpr size_t   kMaxEncryptedRequestSize    = 2048;
constexpr uint64_t kNoDecryptedChunk           = UINT64_MAX;
#endif

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

#if BUILD_WITH_SECURITY_LEVEL >= 1 && defined(BUILD_WITH_ENCRYPTED_PLAYBACK)
enum class DecryptedRecordingKind : uint8_t
{
    Unknown,
    Wav,
    Mp3,
};

struct EncryptedRequestUploadState
{
    AsyncWebServerRequest* request       = nullptr;
    uint8_t*               encrypted     = nullptr;
    size_t                 expected_size = 0;
    size_t                 received_size = 0;
};
#endif

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

#if BUILD_WITH_SECURITY_LEVEL >= 1 && defined(BUILD_WITH_ENCRYPTED_PLAYBACK)
EncryptedRequestUploadState g_upload;
#endif

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

#if BUILD_WITH_SECURITY_LEVEL >= 1 && defined(BUILD_WITH_ENCRYPTED_PLAYBACK)
bool begin_upload(AsyncWebServerRequest* request, size_t total);
bool decode_base64url(const String& text, uint8_t*& out, size_t& out_size);
bool decrypt_filename_blob(const uint8_t  session_key[WebServer::kSessionKeySize],
                           const uint8_t* encrypted,
                           size_t         encrypted_size,
                           uint8_t*&      plaintext,
                           size_t&        plaintext_size,
                           int&           status_code,
                           String&        error);
bool decrypt_uploaded_request_body(const uint8_t session_key[WebServer::kSessionKeySize],
                                   uint8_t*&     plaintext,
                                   size_t&       plaintext_size,
                                   int&          status_code,
                                   String&       error);
bool is_encrypted_recording_path(const char* path);
bool normalize_download_path(const String& file_name, char* out, size_t out_size);
bool parse_request(const uint8_t* plaintext,
                   size_t         plaintext_size,
                   char*          file_path,
                   size_t         file_path_size,
                   bool&          stream_inline,
                   int&           status_code,
                   String&        error);
void reset_upload(AsyncWebServerRequest* request);
void send_recording(AsyncWebServerRequest* request, const char* file_path, bool stream_inline);
bool upload_complete(AsyncWebServerRequest* request);
#endif
void note_web_error();
void set_error(AsyncWebServerRequest* request, int status_code, const char* message);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void writeBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total)
{
#if BUILD_WITH_SECURITY_LEVEL >= 1 && defined(BUILD_WITH_ENCRYPTED_PLAYBACK)
    if (!request || len == 0)
    {
        return;
    }

    if (index == 0 && !begin_upload(request, total))
    {
        return;
    }
    if (request->hasAttribute(kErrorAttribute))
    {
        return;
    }
    if (g_upload.request != request || !g_upload.encrypted)
    {
        set_error(request, 500, "Decrypted download upload state lost");
        return;
    }
    if (index + len > g_upload.expected_size)
    {
        set_error(request, 400, "Encrypted download request body length mismatch");
        reset_upload(request);
        return;
    }

    memcpy(g_upload.encrypted + index, data, len);
    const size_t received_end = index + len;
    if (received_end > g_upload.received_size)
    {
        g_upload.received_size = received_end;
    }
#else
    (void)request;
    (void)data;
    (void)len;
    (void)index;
    (void)total;
#endif
}

void finishGet(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

#if BUILD_WITH_SECURITY_LEVEL < 1 || !defined(BUILD_WITH_ENCRYPTED_PLAYBACK)
    note_web_error();
    request->send(404, "text/plain", "Decrypted downloads are not available");
#else
    const AsyncWebParameter* encrypted_param = WebServer::findRequestParam(request, "file_name");
    if (!encrypted_param || encrypted_param->value().isEmpty())
    {
        DBG_LOGW(TAG, "decrypted GET failed: missing encrypted file_name parameter");
        note_web_error();
        request->send(400, "text/plain", "Missing encrypted filename");
        return;
    }

    uint8_t* encrypted      = nullptr;
    size_t   encrypted_size = 0;
    if (!decode_base64url(encrypted_param->value(), encrypted, encrypted_size))
    {
        DBG_LOGW(TAG, "decrypted GET failed: base64url decode failed");
        note_web_error();
        request->send(400, "text/plain", "Encrypted filename encoding is invalid");
        return;
    }

    uint8_t session_key[WebServer::kSessionKeySize] = {};
    if (!WebServer::copyCachedSessionKey(session_key))
    {
        free(encrypted);
        note_web_error();
        request->send(401, "text/plain", "Session key unavailable");
        return;
    }

    int      status_code = 500;
    String   error;
    uint8_t* plaintext      = nullptr;
    size_t   plaintext_size = 0;
    if (!decrypt_filename_blob(session_key, encrypted, encrypted_size, plaintext, plaintext_size, status_code, error))
    {
        mbedtls_platform_zeroize(session_key, sizeof(session_key));
        free(encrypted);
        note_web_error();
        request->send(status_code, "text/plain", error.isEmpty() ? "Encrypted download request failed" : error);
        return;
    }
    mbedtls_platform_zeroize(session_key, sizeof(session_key));
    free(encrypted);

    char       file_path[kFileNameBufferSize] = {};
    const bool parsed =
        normalize_download_path(String(reinterpret_cast<const char*>(plaintext)), file_path, sizeof(file_path)) &&
        is_encrypted_recording_path(file_path);
    free(plaintext);
    if (!parsed)
    {
        note_web_error();
        request->send(400, "text/plain", "Invalid encrypted filename");
        return;
    }

    send_recording(request, file_path, false);
#endif
}

void finish(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

#if BUILD_WITH_SECURITY_LEVEL < 1 || !defined(BUILD_WITH_ENCRYPTED_PLAYBACK)
    note_web_error();
    request->send(404, "text/plain", "Decrypted downloads are not available");
#else
    if (!upload_complete(request))
    {
        return;
    }

    uint8_t                            session_key[WebServer::kSessionKeySize] = {};
    const WebServer::SessionAuthResult auth = WebServer::authenticateSessionRequest(request, session_key);
    if (auth != WebServer::SessionAuthResult::Ok)
    {
        mbedtls_platform_zeroize(session_key, sizeof(session_key));
        reset_upload(request);
        note_web_error();
        request->send(401, "text/plain", WebServer::sessionAuthResultName(auth));
        return;
    }

    int      status_code = 500;
    String   error;
    uint8_t* plaintext      = nullptr;
    size_t   plaintext_size = 0;
    if (!decrypt_uploaded_request_body(session_key, plaintext, plaintext_size, status_code, error))
    {
        mbedtls_platform_zeroize(session_key, sizeof(session_key));
        reset_upload(request);
        note_web_error();
        request->send(status_code, "text/plain", error.isEmpty() ? "Encrypted download request failed" : error);
        return;
    }
    mbedtls_platform_zeroize(session_key, sizeof(session_key));
    reset_upload(request);

    char       file_path[kFileNameBufferSize] = {};
    bool       stream_inline                  = false;
    const bool parsed =
        parse_request(plaintext, plaintext_size, file_path, sizeof(file_path), stream_inline, status_code, error);
    free(plaintext);
    if (!parsed)
    {
        note_web_error();
        request->send(status_code, "text/plain", error.isEmpty() ? "Invalid decrypted download request" : error);
        return;
    }

    send_recording(request, file_path, stream_inline);
#endif
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void note_web_download()
{
    WifiManager::noteWebDownload();
}

void note_web_error()
{
    WifiManager::noteWebError();
}

void set_error(AsyncWebServerRequest* request, int status_code, const char* message)
{
    if (!request || request->hasAttribute(kErrorAttribute))
    {
        return;
    }

    request->setAttribute(kErrorAttribute, message ? message : "Decrypted download failed");
    request->setAttribute(kStatusAttribute, static_cast<long>(status_code));
}

void send_error(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    const String& error       = request->getAttribute(kErrorAttribute);
    const long    status_code = request->getAttribute(kStatusAttribute, 500L);
    note_web_error();
    request->send(static_cast<int>(status_code), "text/plain", error.isEmpty() ? "Decrypted download failed" : error);
}

#if BUILD_WITH_SECURITY_LEVEL >= 1 && defined(BUILD_WITH_ENCRYPTED_PLAYBACK)
uint8_t* allocate_buffer(size_t size)
{
    if (size == 0)
    {
        size = 1;
    }

    void* buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer)
    {
        buffer = malloc(size);
    }
    return static_cast<uint8_t*>(buffer);
}

bool path_has_parent_or_current_segment(const char* path)
{
    const char* cursor = path;
    while (*cursor)
    {
        while (*cursor == '/')
        {
            ++cursor;
        }

        const char* segment = cursor;
        while (*cursor && *cursor != '/')
        {
            ++cursor;
        }

        const size_t length = static_cast<size_t>(cursor - segment);
        if ((length == 1 && segment[0] == '.') || (length == 2 && segment[0] == '.' && segment[1] == '.'))
        {
            return true;
        }
    }

    return false;
}

bool normalize_download_path(const String& file_name, char* out, size_t out_size)
{
    if (!out || out_size == 0)
    {
        return false;
    }
    out[0] = '\0';

    if (file_name.isEmpty() || file_name.indexOf('\\') >= 0)
    {
        return false;
    }

    const char* src = file_name.c_str();
    while (*src == '/')
    {
        ++src;
    }

    if (*src == '\0' || path_has_parent_or_current_segment(src))
    {
        return false;
    }

    const size_t src_len = strlen(src);
    if (src[src_len - 1] == '/' || src_len + 2 > out_size)
    {
        return false;
    }

    out[0] = '/';
    memcpy(out + 1, src, src_len + 1);
    return true;
}

DecryptedRecordingKind decrypted_recording_kind_for_path(const char* path)
{
    if (ends_with_case_insensitive(path, ".rec"))
    {
        return DecryptedRecordingKind::Wav;
    }
    if (ends_with_case_insensitive(path, ".fly"))
    {
        return DecryptedRecordingKind::Mp3;
    }

    return DecryptedRecordingKind::Unknown;
}

bool is_encrypted_recording_path(const char* path)
{
    return decrypted_recording_kind_for_path(path) != DecryptedRecordingKind::Unknown;
}

const char* decrypted_content_type(DecryptedRecordingKind kind, bool stream_inline)
{
    if (!stream_inline)
    {
        return "application/octet-stream";
    }

    return kind == DecryptedRecordingKind::Mp3 ? "audio/mpeg" : "audio/wav";
}

const char* decrypted_content_header(DecryptedRecordingKind kind)
{
    return kind == DecryptedRecordingKind::Mp3 ? "decrypted-mp3" : "decrypted-wav";
}

bool wav_header_valid(const uint8_t* header)
{
    return header && memcmp(header + 0, "RIFF", 4) == 0 && memcmp(header + 8, "WAVE", 4) == 0 &&
           memcmp(header + 12, "fmt ", 4) == 0 && memcmp(header + 36, "data", 4) == 0;
}

void write_le32(uint8_t* dst, uint32_t value)
{
    dst[0] = static_cast<uint8_t>(value & 0xFF);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dst[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dst[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void patch_wav_header_sizes(uint8_t header[WAV_RIFF_HEADER_LENGTH], uint64_t data_bytes)
{
    if (data_bytes > 0xFFFFFFFFULL - 36ULL)
    {
        data_bytes = 0xFFFFFFFFULL - 36ULL;
    }

    const uint32_t data_size = static_cast<uint32_t>(data_bytes);
    write_le32(header + 4, data_size + 36U);
    write_le32(header + 40, data_size);
}

String safe_content_disposition(const char* disposition, const char* filename)
{
    String header(disposition ? disposition : "attachment");
    header += "; filename=\"";

    const char* cursor = filename ? filename : "recording.wav";
    while (*cursor)
    {
        const char c = *cursor++;
        if (c == '"' || c == '\\' || c == '\r' || c == '\n' || static_cast<uint8_t>(c) < 0x20)
        {
            header += "_";
        }
        else
        {
            header += c;
        }
    }

    header += "\"";
    return header;
}

void decrypted_recording_filename(const char* path, DecryptedRecordingKind kind, char* out, size_t out_size)
{
    if (!out || out_size == 0)
    {
        return;
    }

    const char* name = path ? strrchr(path, '/') : nullptr;
    name             = name ? name + 1 : (path ? path : "recording.rec");
    strlcpy(out, name[0] ? name : "recording.rec", out_size);

    const char*  encrypted_ext     = kind == DecryptedRecordingKind::Mp3 ? ".fly" : ".rec";
    const char*  decrypted_ext     = kind == DecryptedRecordingKind::Mp3 ? ".mp3" : ".wav";
    const size_t encrypted_ext_len = strlen(encrypted_ext);
    const size_t decrypted_ext_len = strlen(decrypted_ext);
    const size_t len               = strlen(out);
    if (len >= encrypted_ext_len && equals_case_insensitive(out + len - encrypted_ext_len, encrypted_ext))
    {
        memcpy(out + len - encrypted_ext_len, decrypted_ext, decrypted_ext_len + 1);
    }
    else if (len + decrypted_ext_len + 1 <= out_size)
    {
        memcpy(out + len, decrypted_ext, decrypted_ext_len + 1);
    }
}

void reset_upload(AsyncWebServerRequest* request = nullptr)
{
    if (request && g_upload.request != request)
    {
        return;
    }

    free(g_upload.encrypted);
    g_upload = {};
}

bool begin_upload(AsyncWebServerRequest* request, size_t total)
{
    request->setAttribute(kStartedAttribute, true);
    request->onDisconnect([]() { reset_upload(); });

    const size_t expected_size = total != 0 ? total : request->contentLength();
    if (expected_size == 0)
    {
        set_error(request, 400, "Missing encrypted download request body");
        return false;
    }
    if (expected_size > kMaxEncryptedRequestSize)
    {
        set_error(request, 413, "Encrypted download request body is too large");
        return false;
    }
    if (g_upload.request && g_upload.request != request)
    {
        set_error(request, 409, "Another decrypted download request is already in progress");
        return false;
    }

    reset_upload(request);
    g_upload.encrypted = allocate_buffer(expected_size);
    if (!g_upload.encrypted)
    {
        set_error(request, 500, "Could not allocate encrypted download request buffer");
        return false;
    }

    g_upload.request       = request;
    g_upload.expected_size = expected_size;
    g_upload.received_size = 0;
    return true;
}

bool upload_complete(AsyncWebServerRequest* request)
{
    if (request->hasAttribute(kErrorAttribute))
    {
        reset_upload(request);
        send_error(request);
        return false;
    }
    if (!request->hasAttribute(kStartedAttribute))
    {
        set_error(request, 400, "Missing encrypted download request body");
        send_error(request);
        return false;
    }
    if (g_upload.request != request || !g_upload.encrypted)
    {
        set_error(request, 500, "Decrypted download upload state lost");
        send_error(request);
        return false;
    }
    if (g_upload.received_size != g_upload.expected_size)
    {
        set_error(request, 400, "Encrypted download request body is incomplete");
        reset_upload(request);
        send_error(request);
        return false;
    }
    return true;
}

bool decrypt_request_body(const uint8_t  session_key[WebServer::kSessionKeySize],
                          const uint8_t* encrypted,
                          size_t         encrypted_size,
                          uint8_t*&      plaintext,
                          size_t&        plaintext_size,
                          int&           status_code,
                          String&        error)
{
    plaintext      = nullptr;
    plaintext_size = 0;

    if (!encrypted || encrypted_size < kEncryptedRequestMinSize)
    {
        status_code = 400;
        error       = "Encrypted download request body is too small";
        return false;
    }
    if (memcmp(encrypted, kEncryptedRequestMagic, sizeof(kEncryptedRequestMagic)) != 0)
    {
        status_code = 400;
        error       = "Encrypted download request magic is invalid";
        return false;
    }
    if (encrypted[sizeof(kEncryptedRequestMagic)] != kEncryptedRequestVersion)
    {
        status_code = 400;
        error       = "Encrypted download request version is unsupported";
        return false;
    }
    if (!session_key)
    {
        status_code = 500;
        error       = "Session key unavailable";
        return false;
    }

    const uint8_t* nonce      = encrypted + sizeof(kEncryptedRequestMagic) + 1;
    const uint8_t* ciphertext = encrypted + kEncryptedRequestHeaderSize;
    const uint8_t* tag        = encrypted + encrypted_size - kGcmTagSize;
    plaintext_size            = encrypted_size - kEncryptedRequestHeaderSize - kGcmTagSize;

    plaintext = allocate_buffer(plaintext_size + 1);
    if (!plaintext)
    {
        status_code = 500;
        error       = "Could not allocate download request plaintext buffer";
        return false;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    const int key_result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, session_key, WebServer::kSessionKeySize * 8);
    const int decrypt_result = key_result == 0 ? mbedtls_gcm_auth_decrypt(&gcm,
                                                                          plaintext_size,
                                                                          nonce,
                                                                          kGcmNonceSize,
                                                                          nullptr,
                                                                          0,
                                                                          tag,
                                                                          kGcmTagSize,
                                                                          ciphertext,
                                                                          plaintext)
                                               : key_result;
    mbedtls_gcm_free(&gcm);

    if (decrypt_result != 0)
    {
        free(plaintext);
        plaintext      = nullptr;
        plaintext_size = 0;
        status_code    = 401;
        error          = "Encrypted download request decryption failed";
        return false;
    }

    plaintext[plaintext_size] = '\0';
    return true;
}

bool decrypt_uploaded_request_body(const uint8_t session_key[WebServer::kSessionKeySize],
                                   uint8_t*&     plaintext,
                                   size_t&       plaintext_size,
                                   int&          status_code,
                                   String&       error)
{
    return decrypt_request_body(session_key,
                                g_upload.encrypted,
                                g_upload.expected_size,
                                plaintext,
                                plaintext_size,
                                status_code,
                                error);
}

bool decrypt_filename_blob(const uint8_t  session_key[WebServer::kSessionKeySize],
                           const uint8_t* encrypted,
                           size_t         encrypted_size,
                           uint8_t*&      plaintext,
                           size_t&        plaintext_size,
                           int&           status_code,
                           String&        error)
{
    plaintext      = nullptr;
    plaintext_size = 0;

    if (!session_key)
    {
        status_code = 500;
        error       = "Session key unavailable";
        return false;
    }
    if (!encrypted || encrypted_size <= kGcmNonceSize + kGcmTagSize)
    {
        status_code = 400;
        error       = "Encrypted filename is too small";
        return false;
    }

    plaintext_size = encrypted_size - kGcmNonceSize - kGcmTagSize;
    if (plaintext_size >= kFileNameBufferSize)
    {
        status_code    = 414;
        error          = "Encrypted filename is too long";
        plaintext_size = 0;
        return false;
    }

    plaintext = allocate_buffer(plaintext_size + 1);
    if (!plaintext)
    {
        status_code    = 500;
        error          = "Could not allocate filename plaintext buffer";
        plaintext_size = 0;
        return false;
    }

    const uint8_t* nonce      = encrypted;
    const uint8_t* ciphertext = encrypted + kGcmNonceSize;
    const uint8_t* tag        = encrypted + encrypted_size - kGcmTagSize;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    const int key_result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, session_key, WebServer::kSessionKeySize * 8);
    const int decrypt_result = key_result == 0 ? mbedtls_gcm_auth_decrypt(&gcm,
                                                                          plaintext_size,
                                                                          nonce,
                                                                          kGcmNonceSize,
                                                                          nullptr,
                                                                          0,
                                                                          tag,
                                                                          kGcmTagSize,
                                                                          ciphertext,
                                                                          plaintext)
                                               : key_result;
    mbedtls_gcm_free(&gcm);

    if (decrypt_result != 0)
    {
        free(plaintext);
        plaintext      = nullptr;
        plaintext_size = 0;
        status_code    = 401;
        error          = "Encrypted filename decryption failed";
        return false;
    }

    plaintext[plaintext_size] = '\0';
    return true;
}

int base64url_value(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z')
    {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9')
    {
        return c - '0' + 52;
    }
    if (c == '-')
    {
        return 62;
    }
    if (c == '_')
    {
        return 63;
    }
    return -1;
}

bool decode_base64url(const String& text, uint8_t*& out, size_t& out_size)
{
    out                     = nullptr;
    out_size                = 0;
    const size_t input_size = text.length();
    if (input_size == 0 || input_size > ((kMaxEncryptedRequestSize + 2) / 3) * 4)
    {
        return false;
    }

    for (size_t i = 0; i < input_size; ++i)
    {
        if (base64url_value(text[i]) < 0)
        {
            return false;
        }
    }

    const size_t padded_size = ((input_size + 3) / 4) * 4;
    char*        padded      = static_cast<char*>(malloc(padded_size + 1));
    if (!padded)
    {
        return false;
    }

    for (size_t i = 0; i < input_size; ++i)
    {
        const char c = text[i];
        padded[i]    = c == '-' ? '+' : (c == '_' ? '/' : c);
    }
    for (size_t i = input_size; i < padded_size; ++i)
    {
        padded[i] = '=';
    }
    padded[padded_size] = '\0';

    out = allocate_buffer(kMaxEncryptedRequestSize);
    if (!out)
    {
        free(padded);
        return false;
    }

    size_t    decoded_size = 0;
    const int result       = mbedtls_base64_decode(out,
                                                   kMaxEncryptedRequestSize,
                                                   &decoded_size,
                                                   reinterpret_cast<const unsigned char*>(padded),
                                                   padded_size);
    free(padded);
    if (result != 0)
    {
        free(out);
        out = nullptr;
        return false;
    }

    out_size = decoded_size;
    return true;
}

class DecryptedRecordingStream
{
public:
    DecryptedRecordingStream()
    {
        mbedtls_gcm_init(&m_gcm);
    }

    ~DecryptedRecordingStream()
    {
        mbedtls_gcm_free(&m_gcm);
        m_gcm_ready = false;
        AsyncFsManager::closeFile();
    }

    bool open(const char* path, int& status_code, String& error)
    {
        if (!path || path[0] == '\0')
        {
            status_code = 400;
            error       = "Missing file_name";
            return false;
        }
        if (!AsyncFsManager::isReady())
        {
            status_code = 503;
            error       = "microSD card is not ready";
            return false;
        }
        if (!Aegis::isInitialized() || !Aegis::getFilecryptKey())
        {
            status_code = 503;
            error       = "File decryption key is not ready";
            return false;
        }

        m_kind = decrypted_recording_kind_for_path(path);
        if (m_kind == DecryptedRecordingKind::Unknown)
        {
            status_code = 400;
            error       = "Only .rec and .fly files can be decrypted";
            return false;
        }

        if (m_kind == DecryptedRecordingKind::Mp3)
        {
            m_plaintext_chunk_size = MP3_ENCRYPTED_PLAINTEXT_LENGTH;
            m_encrypted_chunk_size = MP3_ENCRYPTED_CHUNK_LENGTH;
            m_audio_file_offset    = 0;
            m_output_header_size   = 0;
        }
        else
        {
            m_plaintext_chunk_size = WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH;
            m_encrypted_chunk_size = WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH;
            m_audio_file_offset    = WAV_ENCRYPTED_RIFF_HEADER_LENGTH;
            m_output_header_size   = WAV_RIFF_HEADER_LENGTH;
        }

        m_encrypted = AudioFileRecorder::wavEncryptedAudioBuffer();
        m_plaintext = AudioFileRecorder::wavPlaintextAudioBuffer();
        if (!m_encrypted || !m_plaintext || AudioFileRecorder::wavEncryptedAudioBufferSize() < m_encrypted_chunk_size ||
            AudioFileRecorder::wavPlaintextAudioBufferSize() < m_plaintext_chunk_size)
        {
            status_code = 500;
            error       = "Decrypted download buffers are unavailable";
            return false;
        }

        uint64_t file_size = 0;
        if (!AsyncFsManager::openFileForDownload(path, &file_size))
        {
            status_code = 404;
            error       = "File not found";
            return false;
        }

        if (file_size < m_audio_file_offset || (m_kind == DecryptedRecordingKind::Mp3 && file_size == 0))
        {
            AsyncFsManager::closeFile();
            status_code = 400;
            error       = "Encrypted recording is too small";
            return false;
        }

        const uint64_t encrypted_audio_bytes = file_size - m_audio_file_offset;
        if ((encrypted_audio_bytes % m_encrypted_chunk_size) != 0)
        {
            AsyncFsManager::closeFile();
            status_code = 400;
            error       = "Encrypted recording has an invalid chunk length";
            return false;
        }

        const uint64_t encrypted_chunks = encrypted_audio_bytes / m_encrypted_chunk_size;
        m_audio_plain_bytes             = encrypted_chunks * static_cast<uint64_t>(m_plaintext_chunk_size);
        if (m_kind == DecryptedRecordingKind::Wav && m_audio_plain_bytes > 0xFFFFFFFFULL - 36ULL)
        {
            AsyncFsManager::closeFile();
            status_code = 413;
            error       = "Decrypted WAV is too large";
            return false;
        }

        m_output_size = m_output_header_size + m_audio_plain_bytes;
        if (m_output_size != static_cast<uint64_t>(static_cast<size_t>(m_output_size)))
        {
            AsyncFsManager::closeFile();
            status_code = 413;
            error       = "Decrypted recording is too large";
            return false;
        }

        const int key_result =
            mbedtls_gcm_setkey(&m_gcm, MBEDTLS_CIPHER_ID_AES, Aegis::getFilecryptKey(), Aegis::kFilecryptKeySize * 8);
        if (key_result != 0)
        {
            AsyncFsManager::closeFile();
            status_code = 500;
            error       = "File decryption setup failed";
            return false;
        }
        m_gcm_ready = true;

        if (m_kind == DecryptedRecordingKind::Mp3)
        {
            return true;
        }

        if (!read_decrypted_block(0, WAV_RIFF_HEADER_LENGTH, m_header))
        {
            AsyncFsManager::closeFile();
            status_code = 400;
            error       = "Encrypted WAV header decryption failed";
            return false;
        }
        if (!wav_header_valid(m_header))
        {
            AsyncFsManager::closeFile();
            status_code = 400;
            error       = "Encrypted recording does not contain a valid WAV header";
            return false;
        }

        patch_wav_header_sizes(m_header, m_audio_plain_bytes);
        return true;
    }

    DecryptedRecordingKind kind() const
    {
        return m_kind;
    }

    uint64_t outputSize() const
    {
        return m_output_size;
    }

    size_t fill(uint8_t* buffer, size_t max_len, size_t index)
    {
        if (!buffer || max_len == 0 || m_failed || index >= m_output_size)
        {
            return 0;
        }

        uint64_t position  = index;
        size_t   copied    = 0;
        uint64_t remaining = m_output_size - position;
        if (remaining > max_len)
        {
            remaining = max_len;
        }

        while (remaining > 0)
        {
            if (position < m_output_header_size)
            {
                const size_t header_offset = static_cast<size_t>(position);
                size_t       copy_size     = static_cast<size_t>(m_output_header_size - header_offset);
                if (copy_size > remaining)
                {
                    copy_size = static_cast<size_t>(remaining);
                }

                memcpy(buffer + copied, m_header + header_offset, copy_size);
                copied += copy_size;
                position += copy_size;
                remaining -= copy_size;
                continue;
            }

            const uint64_t audio_position = position - m_output_header_size;
            const uint64_t chunk_index    = audio_position / m_plaintext_chunk_size;
            const size_t   chunk_offset   = static_cast<size_t>(audio_position % m_plaintext_chunk_size);
            if (!ensure_audio_chunk(chunk_index))
            {
                DBG_LOGW(TAG,
                         "decrypted stream stopped early: index=%u position=%llu chunk=%llu copied=%u output=%llu",
                         static_cast<unsigned>(index),
                         static_cast<unsigned long long>(position),
                         static_cast<unsigned long long>(chunk_index),
                         static_cast<unsigned>(copied),
                         static_cast<unsigned long long>(m_output_size));
                m_failed = true;
                return copied;
            }

            size_t copy_size = m_plaintext_chunk_size - chunk_offset;
            if (copy_size > remaining)
            {
                copy_size = static_cast<size_t>(remaining);
            }

            memcpy(buffer + copied, m_plaintext + chunk_offset, copy_size);
            copied += copy_size;
            position += copy_size;
            remaining -= copy_size;
        }

        return copied;
    }

private:
    bool read_decrypted_block(uint64_t file_position, size_t plaintext_size, uint8_t* plaintext)
    {
        if (!m_gcm_ready || !m_encrypted || !plaintext)
        {
            DBG_LOGW(TAG,
                     "decrypted block unavailable: gcm=%d encrypted=%p plaintext=%p",
                     m_gcm_ready ? 1 : 0,
                     static_cast<void*>(m_encrypted),
                     static_cast<void*>(plaintext));
            return false;
        }

        const size_t encrypted_size =
            RECORDER_ENCRYPTED_CHUNK_NONCE_LENGTH + plaintext_size + RECORDER_ENCRYPTED_CHUNK_TAG_LENGTH;
        if (encrypted_size > m_encrypted_chunk_size)
        {
            DBG_LOGW(TAG,
                     "decrypted block too large: pos=%llu plain=%u encrypted=%u max=%u",
                     static_cast<unsigned long long>(file_position),
                     static_cast<unsigned>(plaintext_size),
                     static_cast<unsigned>(encrypted_size),
                     static_cast<unsigned>(m_encrypted_chunk_size));
            return false;
        }
        const int bytes_read = AsyncFsManager::readFileChunk(file_position, m_encrypted, encrypted_size);
        if (bytes_read != static_cast<int>(encrypted_size))
        {
            DBG_LOGW(TAG,
                     "decrypted block read failed: pos=%llu wanted=%u read=%d file_size=%llu",
                     static_cast<unsigned long long>(file_position),
                     static_cast<unsigned>(encrypted_size),
                     bytes_read,
                     static_cast<unsigned long long>(AsyncFsManager::openFileSize()));
            return false;
        }

        const uint8_t* nonce          = m_encrypted;
        const uint8_t* ciphertext     = m_encrypted + RECORDER_ENCRYPTED_CHUNK_NONCE_LENGTH;
        const uint8_t* tag            = ciphertext + plaintext_size;
        const int      decrypt_result = mbedtls_gcm_auth_decrypt(&m_gcm,
                                                                 plaintext_size,
                                                                 nonce,
                                                                 RECORDER_ENCRYPTED_CHUNK_NONCE_LENGTH,
                                                                 nullptr,
                                                                 0,
                                                                 tag,
                                                                 RECORDER_ENCRYPTED_CHUNK_TAG_LENGTH,
                                                                 ciphertext,
                                                                 plaintext);
        if (decrypt_result != 0)
        {
            DBG_LOGW(TAG,
                     "decrypted block auth failed: pos=%llu plain=%u result=%d",
                     static_cast<unsigned long long>(file_position),
                     static_cast<unsigned>(plaintext_size),
                     decrypt_result);
            return false;
        }

        return true;
    }

    bool ensure_audio_chunk(uint64_t chunk_index)
    {
        if (m_loaded_chunk == chunk_index)
        {
            return true;
        }
        if (!m_plaintext)
        {
            return false;
        }

        const uint64_t file_position =
            m_audio_file_offset + chunk_index * static_cast<uint64_t>(m_encrypted_chunk_size);
        if (!read_decrypted_block(file_position, m_plaintext_chunk_size, m_plaintext))
        {
            DBG_LOGW(TAG,
                     "decrypted audio chunk failed: chunk=%llu pos=%llu",
                     static_cast<unsigned long long>(chunk_index),
                     static_cast<unsigned long long>(file_position));
            m_loaded_chunk = kNoDecryptedChunk;
            return false;
        }

        m_loaded_chunk = chunk_index;
        return true;
    }

    mbedtls_gcm_context    m_gcm;
    DecryptedRecordingKind m_kind                           = DecryptedRecordingKind::Unknown;
    bool                   m_gcm_ready                      = false;
    uint8_t*               m_encrypted                      = nullptr;
    uint8_t*               m_plaintext                      = nullptr;
    uint8_t                m_header[WAV_RIFF_HEADER_LENGTH] = {};
    size_t                 m_plaintext_chunk_size           = 0;
    size_t                 m_encrypted_chunk_size           = 0;
    uint64_t               m_audio_file_offset              = 0;
    uint64_t               m_output_header_size             = 0;
    uint64_t               m_audio_plain_bytes              = 0;
    uint64_t               m_output_size                    = 0;
    uint64_t               m_loaded_chunk                   = kNoDecryptedChunk;
    bool                   m_failed                         = false;
};

bool parse_request(const uint8_t* plaintext,
                   size_t         plaintext_size,
                   char*          file_path,
                   size_t         file_path_size,
                   bool&          stream_inline,
                   int&           status_code,
                   String&        error)
{
    JsonDocument               doc;
    const DeserializationError json_error =
        deserializeJson(doc, reinterpret_cast<const char*>(plaintext), plaintext_size);
    if (json_error)
    {
        status_code = 400;
        error       = "Decrypted download JSON parse failed: ";
        error += json_error.c_str();
        return false;
    }

    JsonObject root = doc.as<JsonObject>();
    if (root.isNull())
    {
        status_code = 400;
        error       = "Decrypted download JSON root must be an object";
        return false;
    }

    const char* file_name = root["file_name"].as<const char*>();
    if (!file_name || !normalize_download_path(String(file_name), file_path, file_path_size))
    {
        status_code = 400;
        error       = "Missing or invalid file_name";
        return false;
    }
    if (!is_encrypted_recording_path(file_path))
    {
        status_code = 400;
        error       = "Only .rec and .fly files can be decrypted";
        return false;
    }

    stream_inline = false;
    if (!root["action"].isNull())
    {
        const char* action = root["action"].as<const char*>();
        if (!action || action[0] == '\0' || strcmp(action, "download") == 0)
        {
            stream_inline = false;
        }
        else if (strcmp(action, "stream") == 0)
        {
            stream_inline = true;
        }
        else
        {
            status_code = 400;
            error       = "Unsupported decrypted download action";
            return false;
        }
    }

    status_code = 200;
    return true;
}

void send_recording(AsyncWebServerRequest* request, const char* file_path, bool stream_inline)
{
    std::shared_ptr<DecryptedRecordingStream> stream(new DecryptedRecordingStream());
    if (!stream)
    {
        note_web_error();
        request->send(500, "text/plain", "Could not allocate decrypted download stream");
        return;
    }

    int    status_code = 500;
    String error;
    if (!stream->open(file_path, status_code, error))
    {
        note_web_error();
        request->send(status_code, "text/plain", error.isEmpty() ? "Decrypted download failed" : error);
        return;
    }

    char download_name[kFileNameBufferSize] = {};
    decrypted_recording_filename(file_path, stream->kind(), download_name, sizeof(download_name));

    AsyncWebServerResponse* response =
        request->beginResponse(decrypted_content_type(stream->kind(), stream_inline),
                               static_cast<size_t>(stream->outputSize()),
                               [stream](uint8_t* buffer, size_t max_len, size_t index) -> size_t
                               { return stream ? stream->fill(buffer, max_len, index) : 0; });
    if (!response)
    {
        note_web_error();
        request->send(500);
        return;
    }

    response->addHeader("Content-Disposition",
                        safe_content_disposition(stream_inline ? "inline" : "attachment", download_name));
    response->addHeader("X-TheFly-Content", decrypted_content_header(stream->kind()));
    note_web_download();
    request->send(response);
}
#endif

} // namespace

} // namespace DecryptedDownload

#endif
