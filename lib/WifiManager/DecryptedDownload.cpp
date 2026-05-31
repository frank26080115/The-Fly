#include "DecryptedDownload.h"

#ifdef BUILD_WITH_DECRYPTED_DOWNLOAD

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <memory>
#include <stdlib.h>
#include <string.h>

#include "Aegis.h"
#include "AudioFileRecorder.h"
#include "MicroSdCard.h"
#include "WebServer.h"
#include "WifiManager.h"
#include "dbg_log.h"
#include "esp_heap_caps.h"
#include "mbedtls/gcm.h"
#include "mbedtls/platform_util.h"

extern WifiManager* wifi_manager;

namespace DecryptedDownload
{
namespace
{

constexpr size_t      kFileNameBufferSize = 256;
constexpr const char* kErrorAttribute = "decrypted_download_error";
constexpr const char* kStatusAttribute = "decrypted_download_status";
constexpr const char* kStartedAttribute = "decrypted_download_started";

#if BUILD_WITH_SECURITY_LEVEL >= 1 && defined(BUILD_WITH_ENCRYPTED_PLAYBACK)
constexpr size_t      kGcmNonceSize = 12;
constexpr size_t      kGcmTagSize = 16;
constexpr uint8_t     kEncryptedRequestMagic[] = { 'T', 'F', 'G', 'C' };
constexpr uint8_t     kEncryptedRequestVersion = 1;
constexpr size_t      kEncryptedRequestHeaderSize = sizeof(kEncryptedRequestMagic) + 1 + kGcmNonceSize;
constexpr size_t      kEncryptedRequestMinSize = kEncryptedRequestHeaderSize + kGcmTagSize;
constexpr size_t      kMaxEncryptedRequestSize = 2048;
constexpr uint64_t    kNoDecryptedChunk = UINT64_MAX;
#endif

void note_web_download()
{
    if (wifi_manager)
    {
        wifi_manager->noteWebDownload();
    }
}

void note_web_error()
{
    if (wifi_manager)
    {
        wifi_manager->noteWebError();
    }
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

    const String& error = request->getAttribute(kErrorAttribute);
    const long status_code = request->getAttribute(kStatusAttribute, 500L);
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
        if ((length == 1 && segment[0] == '.') ||
            (length == 2 && segment[0] == '.' && segment[1] == '.'))
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

bool equals_case_insensitive(const char* lhs, const char* rhs)
{
    if (!lhs || !rhs)
    {
        return false;
    }

    while (*lhs && *rhs)
    {
        char l = *lhs++;
        char r = *rhs++;
        if (l >= 'A' && l <= 'Z')
        {
            l = static_cast<char>(l - 'A' + 'a');
        }
        if (r >= 'A' && r <= 'Z')
        {
            r = static_cast<char>(r - 'A' + 'a');
        }
        if (l != r)
        {
            return false;
        }
    }

    return *lhs == '\0' && *rhs == '\0';
}

bool ends_with_case_insensitive(const char* text, const char* suffix)
{
    if (!text || !suffix)
    {
        return false;
    }

    const size_t text_len = strlen(text);
    const size_t suffix_len = strlen(suffix);
    if (text_len < suffix_len)
    {
        return false;
    }

    return equals_case_insensitive(text + text_len - suffix_len, suffix);
}

bool is_encrypted_recording_path(const char* path)
{
    return ends_with_case_insensitive(path, ".rec");
}

bool wav_header_valid(const uint8_t* header)
{
    return header &&
           memcmp(header + 0, "RIFF", 4) == 0 &&
           memcmp(header + 8, "WAVE", 4) == 0 &&
           memcmp(header + 12, "fmt ", 4) == 0 &&
           memcmp(header + 36, "data", 4) == 0;
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

void decrypted_wav_filename(const char* path, char* out, size_t out_size)
{
    if (!out || out_size == 0)
    {
        return;
    }

    const char* name = path ? strrchr(path, '/') : nullptr;
    name = name ? name + 1 : (path ? path : "recording.rec");
    strlcpy(out, name[0] ? name : "recording.rec", out_size);

    const size_t len = strlen(out);
    if (len >= 4 && equals_case_insensitive(out + len - 4, ".rec"))
    {
        memcpy(out + len - 4, ".wav", 5);
    }
    else if (len + 5 <= out_size)
    {
        memcpy(out + len, ".wav", 5);
    }
}

struct EncryptedRequestUploadState
{
    AsyncWebServerRequest* request = nullptr;
    uint8_t* encrypted = nullptr;
    size_t expected_size = 0;
    size_t received_size = 0;
};

EncryptedRequestUploadState g_upload;

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
    request->onDisconnect([]() {
        reset_upload();
    });

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

    g_upload.request = request;
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

bool decrypt_request_body(const uint8_t session_key[WebServer::kSessionKeySize],
                          uint8_t*& plaintext,
                          size_t& plaintext_size,
                          int& status_code,
                          String& error)
{
    plaintext = nullptr;
    plaintext_size = 0;

    const uint8_t* encrypted = g_upload.encrypted;
    const size_t encrypted_size = g_upload.expected_size;
    if (!encrypted || encrypted_size < kEncryptedRequestMinSize)
    {
        status_code = 400;
        error = "Encrypted download request body is too small";
        return false;
    }
    if (memcmp(encrypted, kEncryptedRequestMagic, sizeof(kEncryptedRequestMagic)) != 0)
    {
        status_code = 400;
        error = "Encrypted download request magic is invalid";
        return false;
    }
    if (encrypted[sizeof(kEncryptedRequestMagic)] != kEncryptedRequestVersion)
    {
        status_code = 400;
        error = "Encrypted download request version is unsupported";
        return false;
    }
    if (!session_key)
    {
        status_code = 500;
        error = "Session key unavailable";
        return false;
    }

    const uint8_t* nonce = encrypted + sizeof(kEncryptedRequestMagic) + 1;
    const uint8_t* ciphertext = encrypted + kEncryptedRequestHeaderSize;
    const uint8_t* tag = encrypted + encrypted_size - kGcmTagSize;
    plaintext_size = encrypted_size - kEncryptedRequestHeaderSize - kGcmTagSize;

    plaintext = allocate_buffer(plaintext_size + 1);
    if (!plaintext)
    {
        status_code = 500;
        error = "Could not allocate download request plaintext buffer";
        return false;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    const int key_result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, session_key, WebServer::kSessionKeySize * 8);
    const int decrypt_result = key_result == 0
                                   ? mbedtls_gcm_auth_decrypt(&gcm,
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
        plaintext = nullptr;
        plaintext_size = 0;
        status_code = 401;
        error = "Encrypted download request decryption failed";
        return false;
    }

    plaintext[plaintext_size] = '\0';
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
        m_file.close();
    }

    bool open(const char* path, int& status_code, String& error)
    {
        if (!path || path[0] == '\0')
        {
            status_code = 400;
            error = "Missing file_name";
            return false;
        }
        if (!MicroSdCard::isReady())
        {
            status_code = 503;
            error = "microSD card is not ready";
            return false;
        }
        if (!Aegis::isInitialized() || !Aegis::getFilecryptKey())
        {
            status_code = 503;
            error = "File decryption key is not ready";
            return false;
        }

        m_encrypted = AudioFileRecorder::wavEncryptedAudioBuffer();
        m_plaintext = AudioFileRecorder::wavPlaintextAudioBuffer();
        if (!m_encrypted || !m_plaintext ||
            AudioFileRecorder::wavEncryptedAudioBufferSize() < WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH ||
            AudioFileRecorder::wavPlaintextAudioBufferSize() < WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH)
        {
            status_code = 500;
            error = "Decrypted download buffers are unavailable";
            return false;
        }

        if (!m_file.open(path, O_RDONLY) || !m_file.isFile())
        {
            status_code = 404;
            error = "File not found";
            return false;
        }

        const uint64_t file_size = m_file.fileSize();
        if (file_size < WAV_ENCRYPTED_RIFF_HEADER_LENGTH)
        {
            status_code = 400;
            error = "Encrypted recording is too small";
            return false;
        }

        const uint64_t encrypted_audio_bytes = file_size - WAV_ENCRYPTED_RIFF_HEADER_LENGTH;
        if ((encrypted_audio_bytes % WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH) != 0)
        {
            status_code = 400;
            error = "Encrypted recording has an invalid chunk length";
            return false;
        }

        const uint64_t encrypted_chunks = encrypted_audio_bytes / WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH;
        m_audio_plain_bytes = encrypted_chunks * static_cast<uint64_t>(WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH);
        if (m_audio_plain_bytes > 0xFFFFFFFFULL - 36ULL)
        {
            status_code = 413;
            error = "Decrypted WAV is too large";
            return false;
        }

        m_output_size = static_cast<uint64_t>(WAV_RIFF_HEADER_LENGTH) + m_audio_plain_bytes;
        if (m_output_size != static_cast<uint64_t>(static_cast<size_t>(m_output_size)))
        {
            status_code = 413;
            error = "Decrypted WAV is too large";
            return false;
        }

        const int key_result = mbedtls_gcm_setkey(&m_gcm,
                                                  MBEDTLS_CIPHER_ID_AES,
                                                  Aegis::getFilecryptKey(),
                                                  Aegis::kFilecryptKeySize * 8);
        if (key_result != 0)
        {
            status_code = 500;
            error = "File decryption setup failed";
            return false;
        }
        m_gcm_ready = true;

        if (!read_decrypted_block(0, WAV_RIFF_HEADER_LENGTH, m_header))
        {
            status_code = 400;
            error = "Encrypted WAV header decryption failed";
            return false;
        }
        if (!wav_header_valid(m_header))
        {
            status_code = 400;
            error = "Encrypted recording does not contain a valid WAV header";
            return false;
        }

        patch_wav_header_sizes(m_header, m_audio_plain_bytes);
        return true;
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

        uint64_t position = index;
        size_t copied = 0;
        uint64_t remaining = m_output_size - position;
        if (remaining > max_len)
        {
            remaining = max_len;
        }

        while (remaining > 0)
        {
            if (position < WAV_RIFF_HEADER_LENGTH)
            {
                const size_t header_offset = static_cast<size_t>(position);
                size_t copy_size = WAV_RIFF_HEADER_LENGTH - header_offset;
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

            const uint64_t audio_position = position - WAV_RIFF_HEADER_LENGTH;
            const uint64_t chunk_index = audio_position / WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH;
            const size_t chunk_offset = static_cast<size_t>(audio_position % WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH);
            if (!ensure_audio_chunk(chunk_index))
            {
                m_failed = true;
                return copied;
            }

            size_t copy_size = WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH - chunk_offset;
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
            return false;
        }

        const size_t encrypted_size = WAV_ENCRYPTED_CHUNK_NONCE_LENGTH + plaintext_size + WAV_ENCRYPTED_CHUNK_TAG_LENGTH;
        if (encrypted_size > WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH)
        {
            return false;
        }
        if (!m_file.seekSet(file_position))
        {
            return false;
        }

        const int bytes_read = m_file.read(m_encrypted, encrypted_size);
        if (bytes_read != static_cast<int>(encrypted_size))
        {
            return false;
        }

        const uint8_t* nonce = m_encrypted;
        const uint8_t* ciphertext = m_encrypted + WAV_ENCRYPTED_CHUNK_NONCE_LENGTH;
        const uint8_t* tag = ciphertext + plaintext_size;
        return mbedtls_gcm_auth_decrypt(&m_gcm,
                                        plaintext_size,
                                        nonce,
                                        WAV_ENCRYPTED_CHUNK_NONCE_LENGTH,
                                        nullptr,
                                        0,
                                        tag,
                                        WAV_ENCRYPTED_CHUNK_TAG_LENGTH,
                                        ciphertext,
                                        plaintext) == 0;
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

        const uint64_t file_position = static_cast<uint64_t>(WAV_ENCRYPTED_RIFF_HEADER_LENGTH) +
                                       chunk_index * static_cast<uint64_t>(WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH);
        if (!read_decrypted_block(file_position, WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH, m_plaintext))
        {
            m_loaded_chunk = kNoDecryptedChunk;
            return false;
        }

        m_loaded_chunk = chunk_index;
        return true;
    }

    FsFile m_file;
    mbedtls_gcm_context m_gcm;
    bool m_gcm_ready = false;
    uint8_t* m_encrypted = nullptr;
    uint8_t* m_plaintext = nullptr;
    uint8_t m_header[WAV_RIFF_HEADER_LENGTH] = {};
    uint64_t m_audio_plain_bytes = 0;
    uint64_t m_output_size = 0;
    uint64_t m_loaded_chunk = kNoDecryptedChunk;
    bool m_failed = false;
};

bool parse_request(const uint8_t* plaintext,
                   size_t plaintext_size,
                   char* file_path,
                   size_t file_path_size,
                   bool& stream_inline,
                   int& status_code,
                   String& error)
{
    JsonDocument doc;
    const DeserializationError json_error = deserializeJson(doc, reinterpret_cast<const char*>(plaintext), plaintext_size);
    if (json_error)
    {
        status_code = 400;
        error = "Decrypted download JSON parse failed: ";
        error += json_error.c_str();
        return false;
    }

    JsonObject root = doc.as<JsonObject>();
    if (root.isNull())
    {
        status_code = 400;
        error = "Decrypted download JSON root must be an object";
        return false;
    }

    const char* file_name = root["file_name"].as<const char*>();
    if (!file_name || !normalize_download_path(String(file_name), file_path, file_path_size))
    {
        status_code = 400;
        error = "Missing or invalid file_name";
        return false;
    }
    if (!is_encrypted_recording_path(file_path))
    {
        status_code = 400;
        error = "Only .rec files can be decrypted";
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
            error = "Unsupported decrypted download action";
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

    int status_code = 500;
    String error;
    if (!stream->open(file_path, status_code, error))
    {
        note_web_error();
        request->send(status_code, "text/plain", error.isEmpty() ? "Decrypted download failed" : error);
        return;
    }

    char download_name[kFileNameBufferSize] = {};
    decrypted_wav_filename(file_path, download_name, sizeof(download_name));

    AsyncWebServerResponse* response = request->beginResponse(
        stream_inline ? "audio/wav" : "application/octet-stream",
        static_cast<size_t>(stream->outputSize()),
        [stream](uint8_t* buffer, size_t max_len, size_t index) -> size_t {
            return stream ? stream->fill(buffer, max_len, index) : 0;
        });
    if (!response)
    {
        note_web_error();
        request->send(500);
        return;
    }

    response->addHeader("Content-Disposition",
                        safe_content_disposition(stream_inline ? "inline" : "attachment", download_name));
    response->addHeader("X-TheFly-Content", "decrypted-wav");
    note_web_download();
    request->send(response);
}
#endif

} // namespace

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

    uint8_t session_key[WebServer::kSessionKeySize] = {};
    const WebServer::SessionAuthResult auth = WebServer::authenticateSessionRequest(request, session_key);
    if (auth != WebServer::SessionAuthResult::Ok)
    {
        mbedtls_platform_zeroize(session_key, sizeof(session_key));
        reset_upload(request);
        note_web_error();
        request->send(401, "text/plain", WebServer::sessionAuthResultName(auth));
        return;
    }

    int status_code = 500;
    String error;
    uint8_t* plaintext = nullptr;
    size_t plaintext_size = 0;
    if (!decrypt_request_body(session_key, plaintext, plaintext_size, status_code, error))
    {
        mbedtls_platform_zeroize(session_key, sizeof(session_key));
        reset_upload(request);
        note_web_error();
        request->send(status_code, "text/plain", error.isEmpty() ? "Encrypted download request failed" : error);
        return;
    }
    mbedtls_platform_zeroize(session_key, sizeof(session_key));
    reset_upload(request);

    char file_path[kFileNameBufferSize] = {};
    bool stream_inline = false;
    const bool parsed = parse_request(plaintext,
                                      plaintext_size,
                                      file_path,
                                      sizeof(file_path),
                                      stream_inline,
                                      status_code,
                                      error);
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

} // namespace DecryptedDownload

#endif
