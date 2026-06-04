// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "WebFileHandlers.h"

#include <ESPAsyncWebServer.h>

#include <memory>
#include <string.h>

#include "AsyncFsManager.h"
#include "WebServer.h"
#include "WifiManager.h"
#include "dbg_log.h"

namespace WebFileHandlers
{
namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr const char* TAG                     = "WebFileHandlers";
constexpr size_t      kFileNameBufferSize     = 256;
constexpr const char* kUploadErrorAttribute   = "file_upload_error";
constexpr const char* kUploadStatusAttribute  = "file_upload_status";
constexpr const char* kUploadPathAttribute    = "file_upload_path";
constexpr const char* kUploadStartedAttribute = "file_upload_started";
constexpr const char* kUploadRawBodyAttribute = "file_upload_raw_body";
constexpr const char* kUploadSessionAttribute = "file_upload_session";

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

class FileListJsonStream
{
public:
    ~FileListJsonStream()
    {
        close();
    }

    bool open()
    {
        m_finished = false;
        return AsyncFsManager::resetWalk();
    }

    void close()
    {
        if (m_finished)
        {
            return;
        }

        AsyncFsManager::closeWalk();
        m_finished = true;
    }

    size_t fill(uint8_t* buffer, size_t max_len)
    {
        if (!buffer || max_len == 0 || m_finished)
        {
            return 0;
        }

        if (m_pending_offset >= m_pending.length())
        {
            prepareNextChunk();
        }

        const size_t pending_size = m_pending.length() - m_pending_offset;
        const size_t copy_size    = pending_size < max_len ? pending_size : max_len;
        if (copy_size == 0)
        {
            return 0;
        }

        memcpy(buffer, m_pending.c_str() + m_pending_offset, copy_size);
        m_pending_offset += copy_size;
        if (m_end_after_pending && m_pending_offset >= m_pending.length())
        {
            close();
        }
        return copy_size;
    }

private:
    void prepareNextChunk()
    {
        m_pending        = "";
        m_pending_offset = 0;

        if (!m_started)
        {
            m_started = true;
            m_pending = "[";
            return;
        }

        char file_name[kFileNameBufferSize] = {};
        while (true)
        {
            const AsyncFsManager::WalkResult result = AsyncFsManager::walkOne(file_name, sizeof(file_name));
            if (result == AsyncFsManager::WalkResult::End)
            {
                m_pending           = "]";
                m_end_after_pending = true;
                return;
            }
            if (result != AsyncFsManager::WalkResult::File)
            {
                AsyncFsManager::resetWalk();
                m_pending           = "]";
                m_end_after_pending = true;
                return;
            }

            if (!m_first_file)
            {
                m_pending = ",";
            }
            m_pending += WebServer::jsonString(file_name);
            m_first_file = false;
            return;
        }
    }

    String m_pending;
    size_t m_pending_offset    = 0;
    bool   m_started           = false;
    bool   m_first_file        = true;
    bool   m_end_after_pending = false;
    bool   m_finished          = false;
};

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

std::weak_ptr<FileListJsonStream> g_active_file_list_stream;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

void   close_active_file_list_stream();
void   note_web_download();
void   note_web_error();
bool   normalize_upload_path(const String& file_name, char* out, size_t out_size);
bool   path_has_parent_or_current_segment(const char* path);
bool   prepare_file_upload(AsyncWebServerRequest* request, char* upload_path, size_t upload_path_size);
void   remove_partial_upload(AsyncWebServerRequest* request);
String safe_content_disposition(const char* disposition, const char* filename);
void   send_delete_response(AsyncWebServerRequest* request, int status_code, bool ok, const char* message);
void   send_file_upload_error(AsyncWebServerRequest* request);
void   set_upload_error(AsyncWebServerRequest* request, int status_code, const char* message);
uint32_t upload_session_id(AsyncWebServerRequest* request);
void   write_file_upload_bytes(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index);
bool   write_upload_chunk(uint32_t session_id, const uint8_t* data, size_t len, size_t index);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void writeFileUploadBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t)
{
    if (len == 0)
    {
        return;
    }

    if (request && !request->hasAttribute(kUploadRawBodyAttribute))
    {
        request->setAttribute(kUploadRawBodyAttribute, true);
    }
    write_file_upload_bytes(request, data, len, index);
}

void writeFileUploadPart(
    AsyncWebServerRequest* request, const String&, size_t index, uint8_t* data, size_t len, bool final)
{
    if (len == 0 && !final)
    {
        return;
    }

    write_file_upload_bytes(request, data, len, index);
}

void finishFileUpload(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    if (request->hasAttribute(kUploadErrorAttribute))
    {
        send_file_upload_error(request);
        return;
    }

    if (!request->hasAttribute(kUploadStartedAttribute))
    {
        if (request->contentLength() != 0)
        {
            set_upload_error(request, 400, "Missing file contents");
            send_file_upload_error(request);
            return;
        }

        char upload_path[kFileNameBufferSize] = {};
        if (!prepare_file_upload(request, upload_path, sizeof(upload_path)))
        {
            if (!request->hasAttribute(kUploadErrorAttribute))
            {
                set_upload_error(request, 500, "File upload failed");
            }
            send_file_upload_error(request);
            return;
        }
    }

    if (request->hasAttribute(kUploadRawBodyAttribute))
    {
        const uint64_t expected_size = static_cast<uint64_t>(request->contentLength());
        const uint64_t actual_size   = AsyncFsManager::uploadFileSize(upload_session_id(request));
        if (actual_size != expected_size)
        {
            DBG_LOGW(TAG,
                     "upload size mismatch: expected=%llu actual=%llu",
                     static_cast<unsigned long long>(expected_size),
                     static_cast<unsigned long long>(actual_size));
            set_upload_error(request, 500, "Incomplete file upload");
            send_file_upload_error(request);
            return;
        }
    }

    if (!AsyncFsManager::closeUploadFile(upload_session_id(request)))
    {
        set_upload_error(request, 500, "File close failed");
        send_file_upload_error(request);
        return;
    }

    const String& upload_path = request->getAttribute(kUploadPathAttribute);
    DBG_LOGI(TAG, "uploaded microSD file %s", upload_path.c_str());
    note_web_download();
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void sendMicroSdFile(AsyncWebServerRequest* request)
{
    if (!request)
    {
        DBG_LOGW(TAG, "download failed: null request");
        return;
    }

    if (!request->hasParam("file_name"))
    {
        DBG_LOGW(TAG, "download failed: missing file_name parameter");
        note_web_error();
        request->send(400, "text/plain", "Missing file_name");
        return;
    }

    if (!AsyncFsManager::isReady())
    {
        DBG_LOGW(TAG, "download failed: microSD card is not ready");
        note_web_error();
        request->send(503, "text/plain", "microSD card is not ready");
        return;
    }

    const String file_name = request->getParam("file_name")->value();
    if (file_name.isEmpty())
    {
        DBG_LOGW(TAG, "download failed: empty file_name parameter");
        note_web_error();
        request->send(400, "text/plain", "Missing file_name");
        return;
    }

    close_active_file_list_stream();

    uint64_t file_size = 0;
    if (!AsyncFsManager::openFileForDownload(file_name.c_str(), &file_size))
    {
        DBG_LOGW(TAG, "download failed: openFileForDownload failed path=%s", file_name.c_str());
        note_web_error();
        request->send(404, "text/plain", "File not found");
        return;
    }

    AsyncWebServerResponse* response =
        request->beginResponse("application/octet-stream",
                               static_cast<size_t>(file_size),
                               [file_name, file_size](uint8_t* buffer, size_t max_len, size_t index) -> size_t
                               {
                                   const int bytes_read = AsyncFsManager::readFileChunk(index, buffer, max_len);
                                   if (bytes_read <= 0 && static_cast<uint64_t>(index) < file_size)
                                   {
                                       DBG_LOGW(TAG,
                                                "download chunk failed: path=%s index=%u max=%u read=%d size=%llu",
                                                file_name.c_str(),
                                                static_cast<unsigned>(index),
                                                static_cast<unsigned>(max_len),
                                                bytes_read,
                                                static_cast<unsigned long long>(file_size));
                                   }
                                   return bytes_read > 0 ? static_cast<size_t>(bytes_read) : 0;
                               });
    if (!response)
    {
        DBG_LOGW(TAG,
                 "download failed: beginResponse failed path=%s size=%llu",
                 file_name.c_str(),
                 static_cast<unsigned long long>(file_size));
        AsyncFsManager::closeFile();
        note_web_error();
        request->send(500);
        return;
    }

    request->onDisconnect([]() { AsyncFsManager::closeFile(); });

    response->addHeader("Content-Disposition", safe_content_disposition("attachment", file_name.c_str()));
    DBG_LOGI(TAG, "download started: path=%s size=%llu", file_name.c_str(), static_cast<unsigned long long>(file_size));
    note_web_download();
    request->send(response);
}

void deleteMicroSdFile(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    if (!request->hasParam("file_name"))
    {
        note_web_error();
        send_delete_response(request, 400, false, "Missing file_name");
        return;
    }

    if (!AsyncFsManager::isReady())
    {
        note_web_error();
        send_delete_response(request, 503, false, "microSD card is not ready");
        return;
    }

    const String file_name = request->getParam("file_name")->value();
    if (file_name.isEmpty())
    {
        note_web_error();
        send_delete_response(request, 400, false, "Missing file_name");
        return;
    }

    if (!AsyncFsManager::isFile(file_name.c_str()))
    {
        note_web_error();
        send_delete_response(request, 404, false, "File not found");
        return;
    }

    if (!AsyncFsManager::removeFile(file_name.c_str()))
    {
        DBG_LOGW(TAG, "could not delete microSD file %s", file_name.c_str());
        note_web_error();
        send_delete_response(request, 500, false, "File delete failed");
        return;
    }

    DBG_LOGI(TAG, "deleted microSD file %s", file_name.c_str());
    note_web_download();
    send_delete_response(request, 200, true, "File deleted.");
}

void sendMicroSdFileList(AsyncWebServerRequest* request)
{
    if (!request)
    {
        DBG_LOGW(TAG, "file list failed: null request");
        return;
    }

    if (!AsyncFsManager::isReady())
    {
        DBG_LOGW(TAG, "file list failed: microSD card is not ready");
        note_web_error();
        request->send(503, "text/plain", "microSD card is not ready");
        return;
    }

    close_active_file_list_stream();

    std::shared_ptr<FileListJsonStream> stream(new FileListJsonStream());
    if (!stream || !stream->open())
    {
        DBG_LOGW(TAG, "file list failed: stream open failed");
        note_web_error();
        request->send(500, "text/plain", "File list failed");
        return;
    }
    g_active_file_list_stream = stream;

    request->onDisconnect(
        [stream]()
        {
            if (stream)
            {
                stream->close();
            }
        });

    AsyncWebServerResponse* response =
        request->beginChunkedResponse("application/json",
                                      [stream](uint8_t* buffer, size_t max_len, size_t) -> size_t
                                      { return stream ? stream->fill(buffer, max_len) : 0; });
    if (!response)
    {
        DBG_LOGW(TAG, "file list failed: beginChunkedResponse failed");
        stream->close();
        note_web_error();
        request->send(500);
        return;
    }

    request->send(response);
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

bool normalize_upload_path(const String& file_name, char* out, size_t out_size)
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

void set_upload_error(AsyncWebServerRequest* request, int status_code, const char* message)
{
    if (!request || request->hasAttribute(kUploadErrorAttribute))
    {
        return;
    }

    request->setAttribute(kUploadErrorAttribute, message ? message : "File upload failed");
    request->setAttribute(kUploadStatusAttribute, static_cast<long>(status_code));
}

bool prepare_file_upload(AsyncWebServerRequest* request, char* upload_path, size_t upload_path_size)
{
    if (!request || !upload_path || upload_path_size == 0)
    {
        return false;
    }

    if (!AsyncFsManager::isReady())
    {
        set_upload_error(request, 503, "microSD card is not ready");
        return false;
    }

    const AsyncWebParameter* file_name_param = WebServer::findRequestParam(request, "file_name");
    if (!file_name_param || !normalize_upload_path(file_name_param->value(), upload_path, upload_path_size))
    {
        set_upload_error(request, 400, "Missing or invalid file_name");
        return false;
    }

    const uint64_t expected_size = request->hasAttribute(kUploadRawBodyAttribute)
                                       ? static_cast<uint64_t>(request->contentLength())
                                       : 0;
    uint32_t session_id = 0;
    if (!AsyncFsManager::openFileForUpload(upload_path, expected_size, &session_id))
    {
        set_upload_error(request, 500, "File open failed");
        return false;
    }

    request->setAttribute(kUploadStartedAttribute, true);
    request->setAttribute(kUploadPathAttribute, upload_path);
    request->setAttribute(kUploadSessionAttribute, static_cast<long>(session_id));
    request->onDisconnect([session_id]() { AsyncFsManager::cancelUploadFile(session_id); });
    return true;
}

uint32_t upload_session_id(AsyncWebServerRequest* request)
{
    return request ? static_cast<uint32_t>(request->getAttribute(kUploadSessionAttribute, 0L)) : 0;
}

bool write_upload_chunk(uint32_t session_id, const uint8_t* data, size_t len, size_t index)
{
    return AsyncFsManager::writeUploadFileChunk(session_id, static_cast<uint64_t>(index), data, len);
}

void remove_partial_upload(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    const String& upload_path = request->getAttribute(kUploadPathAttribute);
    AsyncFsManager::cancelUploadFile(upload_session_id(request));
    if (!AsyncFsManager::isReady())
    {
        return;
    }
    if (!upload_path.isEmpty())
    {
        AsyncFsManager::removeFile(upload_path.c_str());
    }
}

void write_file_upload_bytes(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index)
{
    if (!request || request->hasAttribute(kUploadErrorAttribute))
    {
        return;
    }

    char upload_path[kFileNameBufferSize] = {};
    if (index == 0)
    {
        if (!prepare_file_upload(request, upload_path, sizeof(upload_path)))
        {
            return;
        }
    }
    else
    {
        const String& saved_path = request->getAttribute(kUploadPathAttribute);
        if (saved_path.isEmpty() || saved_path.length() >= sizeof(upload_path))
        {
            set_upload_error(request, 500, "File upload state lost");
            return;
        }
        memcpy(upload_path, saved_path.c_str(), saved_path.length() + 1);
    }

    (void)upload_path;
    if (!write_upload_chunk(upload_session_id(request), data, len, index))
    {
        set_upload_error(request, 500, "File chunk write failed");
        AsyncFsManager::cancelUploadFile(upload_session_id(request));
    }
}

void send_file_upload_error(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    remove_partial_upload(request);
    const String& error       = request->getAttribute(kUploadErrorAttribute);
    const long    status_code = request->getAttribute(kUploadStatusAttribute, 500L);
    note_web_error();
    request->send(static_cast<int>(status_code), "text/plain", error.isEmpty() ? "File upload failed" : error);
}

void close_active_file_list_stream()
{
    if (std::shared_ptr<FileListJsonStream> active = g_active_file_list_stream.lock())
    {
        active->close();
    }
    g_active_file_list_stream.reset();
}

void send_delete_response(AsyncWebServerRequest* request, int status_code, bool ok, const char* message)
{
    if (!request)
    {
        return;
    }

    String body = "{\"ok\":";
    body += ok ? "true" : "false";
    body += ",\"message\":\"";
    body += message ? message : "";
    body += "\"}";
    request->send(status_code, "application/json", body);
}

String safe_content_disposition(const char* disposition, const char* filename)
{
    String header(disposition ? disposition : "attachment");
    header += "; filename=\"";

    const char* cursor = filename ? strrchr(filename, '/') : nullptr;
    cursor             = cursor ? cursor + 1 : filename;
    if (!cursor || cursor[0] == '\0')
    {
        cursor = "download";
    }

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

} // namespace

} // namespace WebFileHandlers
