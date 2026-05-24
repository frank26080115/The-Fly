#include "WebServer.h"

#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "Aegis.h"
#include "BluetoothManager.h"
#include "ClockAgent.h"
#ifdef BUILD_FTP_SERVER
#include "FtpServer.h"
#endif
#include "MicroSdCard.h"
#include "WebCfgHandlers.h"
#include "WebFileHandlers.h"
#include "WifiManager.h"
#include "esp_log.h"
#include "thefly_version.h"
#include "web_assets.h"

extern WifiManager* wifi_manager;

namespace
{

constexpr const char* TAG = "WebServer";
constexpr uint32_t    kRequestAuthWindowSeconds = 120;
constexpr size_t      kTimestampLength = 19;
constexpr size_t      kSha256HexLength = Aegis::kSha256Size * 2;

#ifdef BUILD_FTP_SERVER
// This is only a login gate for plain FTP. Replace these credentials before
// exposing FTP; this library is not SFTP and does not encrypt its traffic.
constexpr const char* kFtpUser     = "thefly";
constexpr const char* kFtpPassword = "replace-me";
#endif

AsyncWebServer g_server(80);
bool           g_initialized = false;

bool is_leap_year(int32_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

bool valid_date(int32_t year, int32_t month, int32_t day)
{
    if (year < 2020 || year > 2099 || month < 1 || month > 12)
    {
        return false;
    }

    static constexpr int8_t kMonthDays[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };

    int8_t max_day = kMonthDays[month - 1];
    if (month == 2 && is_leap_year(year))
    {
        ++max_day;
    }

    return day >= 1 && day <= max_day;
}

bool valid_time(int32_t hours, int32_t minutes, int32_t seconds)
{
    return hours >= 0 && hours <= 23 && minutes >= 0 && minutes <= 59 && seconds >= 0 && seconds <= 59;
}

int64_t days_from_civil(int32_t year, int32_t month, int32_t day)
{
    year -= month <= 2;
    const int32_t  era = (year >= 0 ? year : year - 399) / 400;
    const uint32_t yoe = static_cast<uint32_t>(year - era * 400);
    const uint32_t mp  = static_cast<uint32_t>(month + (month > 2 ? -3 : 9));
    const uint32_t doy = (153 * mp + 2) / 5 + static_cast<uint32_t>(day) - 1;
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

int64_t datetime_to_epoch_seconds(const m5::rtc_datetime_t& datetime)
{
    return days_from_civil(datetime.date.year, datetime.date.month, datetime.date.date) * 86400LL +
           static_cast<int64_t>(datetime.time.hours) * 3600LL +
           static_cast<int64_t>(datetime.time.minutes) * 60LL +
           datetime.time.seconds;
}

bool parse_digits(const char* text, size_t offset, size_t count, int32_t& out)
{
    int32_t value = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const char ch = text[offset + i];
        if (!isdigit(static_cast<unsigned char>(ch)))
        {
            return false;
        }
        value = value * 10 + (ch - '0');
    }
    out = value;
    return true;
}

bool parse_auth_timestamp(const char* text, int64_t& epoch_seconds)
{
    if (!text || strlen(text) != kTimestampLength ||
        text[4] != '-' ||
        text[7] != '-' ||
        text[10] != '-' ||
        text[13] != ':' ||
        text[16] != ':')
    {
        return false;
    }

    int32_t year = 0;
    int32_t month = 0;
    int32_t day = 0;
    int32_t hours = 0;
    int32_t minutes = 0;
    int32_t seconds = 0;
    if (!parse_digits(text, 0, 4, year) ||
        !parse_digits(text, 5, 2, month) ||
        !parse_digits(text, 8, 2, day) ||
        !parse_digits(text, 11, 2, hours) ||
        !parse_digits(text, 14, 2, minutes) ||
        !parse_digits(text, 17, 2, seconds) ||
        !valid_date(year, month, day) ||
        !valid_time(hours, minutes, seconds))
    {
        return false;
    }

    m5::rtc_datetime_t datetime = {};
    datetime.date.year = static_cast<int16_t>(year);
    datetime.date.month = static_cast<int8_t>(month);
    datetime.date.date = static_cast<int8_t>(day);
    datetime.time.hours = static_cast<int8_t>(hours);
    datetime.time.minutes = static_cast<int8_t>(minutes);
    datetime.time.seconds = static_cast<int8_t>(seconds);
    epoch_seconds = datetime_to_epoch_seconds(datetime);
    return true;
}

int8_t hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    return -1;
}

bool hmac_sha256_hex_matches(const char* timestamp, const char* expected_hash)
{
    if (!timestamp || !expected_hash || strlen(expected_hash) != kSha256HexLength)
    {
        return false;
    }

    if (!Aegis::isInitialized() && !Aegis::init())
    {
        return false;
    }

    const uint8_t* master_key = Aegis::getMasterKey();
    if (!master_key)
    {
        return false;
    }

    uint8_t digest[Aegis::kSha256Size] = {};
    if (!Aegis::hmacSha256(master_key,
                           Aegis::kMasterKeySize,
                           reinterpret_cast<const uint8_t*>(timestamp),
                           strlen(timestamp),
                           digest))
    {
        return false;
    }

    uint8_t diff = 0;
    for (size_t i = 0; i < Aegis::kSha256Size; ++i)
    {
        const int8_t high = hex_nibble(expected_hash[i * 2]);
        const int8_t low  = hex_nibble(expected_hash[i * 2 + 1]);
        if (high < 0 || low < 0)
        {
            return false;
        }

        diff |= digest[i] ^ static_cast<uint8_t>((high << 4) | low);
    }

    return diff == 0;
}

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

void append_json_u64(String& json, uint64_t value)
{
    char text[24] = {};
    snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
    json += text;
}

bool ip_is_zero(const IPAddress& ip)
{
    return static_cast<uint32_t>(ip) == 0;
}

String self_ip_string()
{
    const IPAddress station_ip = WiFi.localIP();
    if (!ip_is_zero(station_ip))
    {
        return station_ip.toString();
    }

    const IPAddress soft_ap_ip = WiFi.softAPIP();
    return ip_is_zero(soft_ap_ip) ? String("") : soft_ap_ip.toString();
}

void send_info(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    esp_bd_addr_t bdaddr = {};
    char          bdaddr_text[18] = "unknown";
    if (BtManager::localBdaddr(bdaddr))
    {
        WebServer::formatMac(bdaddr, bdaddr_text, sizeof(bdaddr_text));
    }

    const bool ready = MicroSdCard::isReady();
    const MicroSdCard::Health health = ready ? MicroSdCard::health() : MicroSdCard::Health::NotReady;
    const uint64_t total_bytes = ready ? MicroSdCard::totalBytes() : 0;
    const uint64_t used_bytes  = ready ? MicroSdCard::usedBytes() : 0;
    const uint64_t free_bytes  = ready ? MicroSdCard::freeBytes() : 0;
    const bool default_soft_ap = wifi_manager && wifi_manager->isGeneratedSoftApActive();

    String json;
    json.reserve(640);
    json += "{";
    json += "\"device_name\":";
    json += WebServer::jsonString(BtManager::localDeviceName());
    json += ",\"bdaddr\":";
    json += WebServer::jsonString(bdaddr_text);
    json += ",\"wifi_mac\":";
    json += WebServer::jsonString(WiFi.macAddress().c_str());
    json += ",\"self_ip\":";
    json += WebServer::jsonString(self_ip_string().c_str());
    json += ",\"default_soft_ap\":";
    json += default_soft_ap ? "true" : "false";
    json += ",\"firmware\":";
    json += WebServer::jsonString(version_str);
    json += ",\"compiler\":";
    json += WebServer::jsonString(compiler_version_str);
    json += ",\"compiled\":";
    json += WebServer::jsonString(compiler_time_str);
    json += ",\"disk\":{";
    json += "\"ready\":";
    json += ready ? "true" : "false";
    json += ",\"health\":";
    json += WebServer::jsonString(MicroSdCard::healthName(health));
    json += ",\"total_bytes\":";
    append_json_u64(json, total_bytes);
    json += ",\"used_bytes\":";
    append_json_u64(json, used_bytes);
    json += ",\"free_bytes\":";
    append_json_u64(json, free_bytes);
    json += "}}";

    request->send(200, "application/json", json);
}

} // namespace

const char* WebServer::requestAuthResultName(RequestAuthResult result)
{
    switch (result)
    {
    case RequestAuthResult::Ok:
        return "Ok";
    case RequestAuthResult::MissingTimestamp:
        return "Missing timestamp";
    case RequestAuthResult::BadTimestamp:
        return "Bad timestamp";
    case RequestAuthResult::ClockNotReady:
        return "Clock not ready";
    case RequestAuthResult::TimestampOutsideWindow:
        return "Timestamp outside allowed window";
    case RequestAuthResult::MissingHash:
        return "Missing hash";
    case RequestAuthResult::BadHash:
        return "Bad hash";
    case RequestAuthResult::MasterKeyUnavailable:
        return "Master key unavailable";
    case RequestAuthResult::HashFailed:
        return "Hash failed";
    case RequestAuthResult::HashMismatch:
        return "Hash mismatch";
    default:
        return "Unknown authentication error";
    }
}

String WebServer::jsonString(const char* text)
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

void WebServer::formatMac(const uint8_t* bdaddr, char* buffer, size_t buffer_size)
{
    if (!bdaddr || !buffer || buffer_size == 0)
    {
        return;
    }

    snprintf(buffer,
             buffer_size,
             "%02X:%02X:%02X:%02X:%02X:%02X",
             bdaddr[0],
             bdaddr[1],
             bdaddr[2],
             bdaddr[3],
             bdaddr[4],
             bdaddr[5]);
}

const AsyncWebParameter* WebServer::findRequestParam(AsyncWebServerRequest* request, const char* name)
{
    if (!request || !name)
    {
        return nullptr;
    }

    const AsyncWebParameter* param = request->getParam(name, false, false);
    if (param)
    {
        return param;
    }

    return request->getParam(name, true, false);
}

WebServer::RequestAuthResult WebServer::authenticateRequest(AsyncWebServerRequest* request)
{
    const AsyncWebParameter* timestamp_param = findRequestParam(request, "timestamp");
    if (!timestamp_param || timestamp_param->value().isEmpty())
    {
        return RequestAuthResult::MissingTimestamp;
    }

    const AsyncWebParameter* hash_param = findRequestParam(request, "hash");
    if (!hash_param || hash_param->value().isEmpty())
    {
        return RequestAuthResult::MissingHash;
    }

    int64_t request_epoch = 0;
    if (!parse_auth_timestamp(timestamp_param->value().c_str(), request_epoch))
    {
        return RequestAuthResult::BadTimestamp;
    }

    m5::rtc_datetime_t now = {};
    if (!Clock.getDateTime(&now))
    {
        return RequestAuthResult::ClockNotReady;
    }

    const int64_t now_epoch = datetime_to_epoch_seconds(now);
    const int64_t delta     = request_epoch > now_epoch ? request_epoch - now_epoch : now_epoch - request_epoch;
    if (delta > kRequestAuthWindowSeconds)
    {
        return RequestAuthResult::TimestampOutsideWindow;
    }

    if (hash_param->value().length() != kSha256HexLength)
    {
        return RequestAuthResult::BadHash;
    }

    if (!Aegis::isInitialized() && !Aegis::init())
    {
        return RequestAuthResult::MasterKeyUnavailable;
    }

    if (!Aegis::getMasterKey())
    {
        return RequestAuthResult::MasterKeyUnavailable;
    }

    if (!hmac_sha256_hex_matches(timestamp_param->value().c_str(), hash_param->value().c_str()))
    {
        return RequestAuthResult::HashMismatch;
    }

    return RequestAuthResult::Ok;
}

bool WebServer::requestIsAuthenticated(AsyncWebServerRequest* request)
{
    return authenticateRequest(request) == RequestAuthResult::Ok;
}

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

    g_server.on(AsyncURIMatcher::exact("/get_info"), HTTP_GET, send_info);
    ESP_LOGI(TAG, "registered device info GET /get_info");

    g_server.on(AsyncURIMatcher::exact("/download_file"), HTTP_GET, WebFileHandlers::sendMicroSdFile);
    ESP_LOGI(TAG, "registered microSD download GET /download_file");

    g_server.on(AsyncURIMatcher::exact("/delete_file"), HTTP_GET, WebFileHandlers::deleteMicroSdFile);
    ESP_LOGI(TAG, "registered microSD delete GET /delete_file");

    g_server.on(AsyncURIMatcher::exact("/list_files.json"), HTTP_GET, WebFileHandlers::sendMicroSdFileList);
    ESP_LOGI(TAG, "registered microSD file list GET /list_files.json");

    g_server.on(AsyncURIMatcher::exact("/get_cfg"), HTTP_GET, WebCfgHandlers::sendCfg);
    ESP_LOGI(TAG, "registered authenticated config GET /get_cfg");

    g_server.on(AsyncURIMatcher::exact("/set_cfg"), HTTP_POST, WebCfgHandlers::finishSetCfg, nullptr, WebCfgHandlers::writeSetCfgBody);
    ESP_LOGI(TAG, "registered authenticated encrypted config POST /set_cfg");

    // Authenticates timestamp/hash metadata, but the uploaded HTTP body is plaintext.
    g_server.on(AsyncURIMatcher::exact("/file_upload"),
                HTTP_POST,
                WebFileHandlers::finishFileUpload,
                WebFileHandlers::writeFileUploadPart,
                WebFileHandlers::writeFileUploadBody);
    ESP_LOGI(TAG, "registered authenticated microSD upload POST /file_upload");

#ifdef BUILD_FTP_SERVER
    if (!FtpServer::start(MicroSdCard::fs(), kFtpUser, kFtpPassword))
    {
        ESP_LOGE(TAG, "FTP server start failed");
        return false;
    }

    ESP_LOGI(TAG, "FTP server started");
#endif

    g_server.begin();
    g_initialized = true;
    ESP_LOGI(TAG, "web server started");

    return true;
}
