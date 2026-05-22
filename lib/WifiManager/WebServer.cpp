#include "WebServer.h"

#include <ESPAsyncWebServer.h>

#include <memory>
#include <string.h>

#include "MicroSdCard.h"
#include "esp_log.h"
#include "web_assets.h"

namespace
{

constexpr const char* TAG = "WebServer";
constexpr size_t      kFileNameBufferSize = 256;

AsyncWebServer g_server(80);
bool           g_initialized = false;

String json_string(const char* text)
{
    String encoded("\"");
    if (!text)
    {
        encoded += "\"";
        return encoded;
    }

    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(text); *cursor; ++cursor)
    {
        switch (*cursor)
        {
        case '"':
            encoded += "\\\"";
            break;
        case '\\':
            encoded += "\\\\";
            break;
        case '\b':
            encoded += "\\b";
            break;
        case '\f':
            encoded += "\\f";
            break;
        case '\n':
            encoded += "\\n";
            break;
        case '\r':
            encoded += "\\r";
            break;
        case '\t':
            encoded += "\\t";
            break;
        default:
            if (*cursor < 0x20)
            {
                char escaped[7];
                snprintf(escaped, sizeof(escaped), "\\u%04X", static_cast<unsigned>(*cursor));
                encoded += escaped;
            }
            else
            {
                encoded += static_cast<char>(*cursor);
            }
            break;
        }
    }

    encoded += "\"";
    return encoded;
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
            m_pending += json_string(file_name);
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

const web_asset_desc_t* find_asset_by_name(const char* file_name)
{
    if (!file_name || file_name[0] == '\0')
    {
        return nullptr;
    }

    for (size_t i = 0; i < WEB_ASSETS_CNT; ++i)
    {
        if (strcmp(web_assets_list[i].file_name, file_name) == 0)
        {
            return &web_assets_list[i];
        }
    }

    return nullptr;
}

const web_asset_desc_t* find_asset_for_url(const String& url)
{
    if (url == "/")
    {
        return find_asset_by_name("index.html");
    }

    const char* path = url.c_str();
    while (*path == '/')
    {
        ++path;
    }
    return find_asset_by_name(path);
}

String asset_route(const web_asset_desc_t& asset)
{
    String route("/");
    route += asset.file_name;
    return route;
}

void send_asset(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    const web_asset_desc_t* asset = find_asset_for_url(request->url());
    if (!asset || !asset->ptr_payload || asset->compressed_size == 0)
    {
        request->send(404);
        return;
    }

    AsyncWebServerResponse* response =
        request->beginResponse(200, asset->mime_type, asset->ptr_payload, asset->compressed_size);
    if (!response)
    {
        request->send(500);
        return;
    }

    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

void send_micro_sd_file(AsyncWebServerRequest* request)
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

void delete_micro_sd_file(AsyncWebServerRequest* request)
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

void send_micro_sd_file_list(AsyncWebServerRequest* request)
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

} // namespace

bool WebServer::init()
{
    if (g_initialized)
    {
        return true;
    }

    for (size_t i = 0; i < WEB_ASSETS_CNT; ++i)
    {
        const String route = asset_route(web_assets_list[i]);
        g_server.on(AsyncURIMatcher::exact(route), HTTP_GET, send_asset);
        ESP_LOGI(TAG, "registered web asset GET %s", route.c_str());
    }

    if (find_asset_by_name("index.html"))
    {
        g_server.on(AsyncURIMatcher::exact("/"), HTTP_GET, send_asset);
        ESP_LOGI(TAG, "registered web asset GET /");
    }

    g_server.on(AsyncURIMatcher::exact("/download_file"), HTTP_GET, send_micro_sd_file);
    ESP_LOGI(TAG, "registered microSD download GET /download_file");

    g_server.on(AsyncURIMatcher::exact("/delete_file"), HTTP_GET, delete_micro_sd_file);
    ESP_LOGI(TAG, "registered microSD delete GET /delete_file");

    g_server.on(AsyncURIMatcher::exact("/list_files.json"), HTTP_GET, send_micro_sd_file_list);
    ESP_LOGI(TAG, "registered microSD file list GET /list_files.json");

    g_server.begin();
    g_initialized = true;
    ESP_LOGI(TAG, "web server started");
    return true;
}
