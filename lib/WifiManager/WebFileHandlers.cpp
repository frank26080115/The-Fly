#include "WebFileHandlers.h"

#include <ESPAsyncWebServer.h>

#include <memory>
#include <string.h>

#include "MicroSdCard.h"
#include "WebServer.h"
#include "esp_log.h"

namespace WebFileHandlers
{
namespace
{

constexpr const char* TAG = "WebFileHandlers";
constexpr size_t      kFileNameBufferSize = 256;
constexpr const char* kUploadErrorAttribute = "file_upload_error";
constexpr const char* kUploadStatusAttribute = "file_upload_status";
constexpr const char* kUploadPathAttribute = "file_upload_path";
constexpr const char* kUploadStartedAttribute = "file_upload_started";

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

    request->setAttribute(kUploadStartedAttribute, true);

    const WebServer::RequestAuthResult auth = WebServer::authenticateRequest(request);
    if (auth != WebServer::RequestAuthResult::Ok)
    {
        set_upload_error(request, 401, WebServer::requestAuthResultName(auth));
        return false;
    }

    if (!MicroSdCard::isReady())
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

    request->setAttribute(kUploadPathAttribute, upload_path);
    return true;
}

bool write_upload_chunk(const char* upload_path, const uint8_t* data, size_t len, bool first_chunk)
{
    FsFile file;
    const oflag_t flags = O_WRONLY | O_CREAT | (first_chunk ? O_TRUNC : O_APPEND);
    if (!file.open(upload_path, flags))
    {
        return false;
    }

    const size_t written = len == 0 ? 0 : file.write(data, len);
    const bool   ok      = written == len && file.sync();
    file.close();
    return ok;
}

void remove_partial_upload(AsyncWebServerRequest* request)
{
    if (!request || !MicroSdCard::isReady())
    {
        return;
    }

    const String& upload_path = request->getAttribute(kUploadPathAttribute);
    if (!upload_path.isEmpty() && MicroSdCard::fs().exists(upload_path.c_str()))
    {
        MicroSdCard::fs().remove(upload_path.c_str());
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

    if (!write_upload_chunk(upload_path, data, len, index == 0))
    {
        set_upload_error(request, 500, "File write failed");
    }
}

void send_file_upload_error(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    remove_partial_upload(request);
    const String& error = request->getAttribute(kUploadErrorAttribute);
    const long    status_code = request->getAttribute(kUploadStatusAttribute, 500L);
    request->send(static_cast<int>(status_code), "text/plain", error.isEmpty() ? "File upload failed" : error);
}

class FileListJsonStream
{
public:
    ~FileListJsonStream()
    {
        m_child.close();
        m_root.close();
    }

    bool open()
    {
        return m_root.open("/", O_RDONLY);
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
            m_finished = true;
            m_child.close();
            m_root.close();
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
        while (m_child.openNext(&m_root, O_RDONLY))
        {
            if (!m_child.isFile())
            {
                m_child.close();
                continue;
            }

            m_child.getName(file_name, sizeof(file_name));
            m_child.close();
            if (file_name[0] == '\0')
            {
                continue;
            }

            if (!m_first_file)
            {
                m_pending = ",";
            }
            m_pending += WebServer::jsonString(file_name);
            m_first_file = false;
            return;
        }

        m_pending           = "]";
        m_end_after_pending = true;
    }

    FsFile m_root;
    FsFile m_child;
    String m_pending;
    size_t m_pending_offset   = 0;
    bool   m_started          = false;
    bool   m_first_file       = true;
    bool   m_end_after_pending = false;
    bool   m_finished         = false;
};

} // namespace

void writeFileUploadBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t)
{
    if (len == 0)
    {
        return;
    }

    write_file_upload_bytes(request, data, len, index);
}

void writeFileUploadPart(AsyncWebServerRequest* request, const String&, size_t index, uint8_t* data, size_t len, bool final)
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
        if (!prepare_file_upload(request, upload_path, sizeof(upload_path)) ||
            !write_upload_chunk(upload_path, nullptr, 0, true))
        {
            if (!request->hasAttribute(kUploadErrorAttribute))
            {
                set_upload_error(request, 500, "File write failed");
            }
            send_file_upload_error(request);
            return;
        }
    }

    const String& upload_path = request->getAttribute(kUploadPathAttribute);
    ESP_LOGI(TAG, "uploaded microSD file %s", upload_path.c_str());
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void sendMicroSdFile(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    if (!request->hasParam("file_name"))
    {
        request->send(400, "text/plain", "Missing file_name");
        return;
    }

    if (!MicroSdCard::isReady())
    {
        request->send(503, "text/plain", "microSD card is not ready");
        return;
    }

    const String file_name = request->getParam("file_name")->value();
    if (file_name.isEmpty())
    {
        request->send(400, "text/plain", "Missing file_name");
        return;
    }

    std::shared_ptr<FsFile> file(new FsFile());
    if (!file || !file->open(file_name.c_str(), O_RDONLY) || file->isDir())
    {
        request->send(404, "text/plain", "File not found");
        return;
    }

    AsyncWebServerResponse* response = request->beginChunkedResponse(
        "application/octet-stream",
        [file](uint8_t* buffer, size_t max_len, size_t index) -> size_t {
            if (!file || !file->seekSet(index))
            {
                return 0;
            }

            const int bytes_read = file->read(buffer, max_len);
            return bytes_read > 0 ? static_cast<size_t>(bytes_read) : 0;
        });
    if (!response)
    {
        request->send(500);
        return;
    }

    response->addHeader("Content-Disposition", "attachment");
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
        request->send(400, "text/plain", "Missing file_name");
        return;
    }

    if (!MicroSdCard::isReady())
    {
        request->send(503, "text/plain", "microSD card is not ready");
        return;
    }

    const String file_name = request->getParam("file_name")->value();
    if (file_name.isEmpty())
    {
        request->send(400, "text/plain", "Missing file_name");
        return;
    }

    FsFile file;
    if (!file.open(file_name.c_str(), O_RDONLY))
    {
        request->send(404, "text/plain", "File not found");
        return;
    }

    if (file.isDir())
    {
        request->send(400, "text/plain", "Target is not a file");
        return;
    }

    if (!file.remove())
    {
        ESP_LOGW(TAG, "could not delete microSD file %s", file_name.c_str());
        request->send(500, "text/plain", "File delete failed");
        return;
    }

    ESP_LOGI(TAG, "deleted microSD file %s", file_name.c_str());
    request->send(200, "text/plain", "Deleted");
}

void sendMicroSdFileList(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    if (!MicroSdCard::isReady())
    {
        request->send(503, "text/plain", "microSD card is not ready");
        return;
    }

    std::shared_ptr<FileListJsonStream> stream(new FileListJsonStream());
    if (!stream || !stream->open())
    {
        request->send(500, "text/plain", "File list failed");
        return;
    }

    AsyncWebServerResponse* response = request->beginChunkedResponse(
        "application/json",
        [stream](uint8_t* buffer, size_t max_len, size_t) -> size_t {
            return stream ? stream->fill(buffer, max_len) : 0;
        });
    if (!response)
    {
        request->send(500);
        return;
    }

    request->send(response);
}

} // namespace WebFileHandlers
