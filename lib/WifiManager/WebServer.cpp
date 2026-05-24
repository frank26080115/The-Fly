#include "WebServer.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <ctype.h>
#include <memory>
#include <stdlib.h>
#include <string.h>

#include "Aegis.h"
#include "BtHostList.h"
#include "ClockAgent.h"
#ifdef BUILD_FTP_SERVER
#include "FtpServer.h"
#endif
#include "IconLookup.h"
#include "MicroSdCard.h"
#include "WifiManager.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "mbedtls/gcm.h"
#include "utilfuncs.h"
#include "web_assets.h"

extern WifiManager* wifi_manager;
extern BtHostList*  bt_host_list;

namespace
{

constexpr const char* TAG = "WebServer";
constexpr size_t      kFileNameBufferSize = 256;
constexpr uint32_t    kRequestAuthWindowSeconds = 120;
constexpr size_t      kTimestampLength = 19;
constexpr size_t      kSha256HexLength = Aegis::kSha256Size * 2;
constexpr const char* kUploadErrorAttribute = "file_upload_error";
constexpr const char* kUploadStatusAttribute = "file_upload_status";
constexpr const char* kUploadPathAttribute = "file_upload_path";
constexpr const char* kUploadStartedAttribute = "file_upload_started";
constexpr size_t      kGcmNonceSize = 12;
constexpr size_t      kGcmTagSize = 16;
constexpr size_t      kGcmRecordHeaderSize = 2 + kGcmNonceSize + kGcmTagSize;
constexpr size_t      kCfgPlainChunkMaxSize = 768;
constexpr size_t      kSetCfgMaxEncryptedSize = 64 * 1024;
constexpr uint8_t     kSetCfgMagic[] = { 'T', 'F', 'G', 'C' };
constexpr uint8_t     kSetCfgVersion = 1;
constexpr size_t      kSetCfgHeaderSize = sizeof(kSetCfgMagic) + 1 + kGcmNonceSize;
constexpr size_t      kSetCfgMinEnvelopeSize = kSetCfgHeaderSize + kGcmTagSize;
constexpr const char* kSetCfgErrorAttribute = "set_cfg_error";
constexpr const char* kSetCfgStatusAttribute = "set_cfg_status";
constexpr const char* kSetCfgStartedAttribute = "set_cfg_started";

#ifdef BUILD_FTP_SERVER
// This is only a login gate for plain FTP. Replace these credentials before
// exposing FTP; this library is not SFTP and does not encrypt its traffic.
constexpr const char* kFtpUser     = "thefly";
constexpr const char* kFtpPassword = "replace-me";
#endif

AsyncWebServer g_server(80);
bool           g_initialized = false;

enum class RequestAuthResult
{
    Ok,
    MissingTimestamp,
    BadTimestamp,
    ClockNotReady,
    TimestampOutsideWindow,
    MissingHash,
    BadHash,
    MasterKeyUnavailable,
    HashFailed,
    HashMismatch,
};

const char* request_auth_result_name(RequestAuthResult result)
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

void store_u16_be(uint8_t* out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value >> 8);
    out[1] = static_cast<uint8_t>(value & 0xFF);
}

void store_u32_be(uint8_t* out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value >> 24);
    out[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    out[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(value & 0xFF);
}

void format_mac(const esp_bd_addr_t bdaddr, char* buffer, size_t buffer_size)
{
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

const AsyncWebParameter* find_request_param(AsyncWebServerRequest* request, const char* name)
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

RequestAuthResult authenticate_request(AsyncWebServerRequest* request)
{
    const AsyncWebParameter* timestamp_param = find_request_param(request, "timestamp");
    if (!timestamp_param || timestamp_param->value().isEmpty())
    {
        return RequestAuthResult::MissingTimestamp;
    }

    const AsyncWebParameter* hash_param = find_request_param(request, "hash");
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

bool request_is_authenticated(AsyncWebServerRequest* request)
{
    return authenticate_request(request) == RequestAuthResult::Ok;
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

    const RequestAuthResult auth = authenticate_request(request);
    if (auth != RequestAuthResult::Ok)
    {
        set_upload_error(request, 401, request_auth_result_name(auth));
        return false;
    }

    if (!MicroSdCard::isReady())
    {
        set_upload_error(request, 503, "microSD card is not ready");
        return false;
    }

    const AsyncWebParameter* file_name_param = find_request_param(request, "file_name");
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

void write_file_upload_body(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t)
{
    if (len == 0)
    {
        return;
    }

    write_file_upload_bytes(request, data, len, index);
}

void write_file_upload_part(AsyncWebServerRequest* request, const String&, size_t index, uint8_t* data, size_t len, bool final)
{
    if (len == 0 && !final)
    {
        return;
    }

    write_file_upload_bytes(request, data, len, index);
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

void finish_file_upload(AsyncWebServerRequest* request)
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

void set_cfg_error(AsyncWebServerRequest* request, int status_code, const char* message)
{
    if (!request || request->hasAttribute(kSetCfgErrorAttribute))
    {
        return;
    }

    request->setAttribute(kSetCfgErrorAttribute, message ? message : "Config update failed");
    request->setAttribute(kSetCfgStatusAttribute, static_cast<long>(status_code));
}

void send_set_cfg_error(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    const String& error = request->getAttribute(kSetCfgErrorAttribute);
    const long    status_code = request->getAttribute(kSetCfgStatusAttribute, 500L);
    request->send(static_cast<int>(status_code), "text/plain", error.isEmpty() ? "Config update failed" : error);
}

#ifndef BUILD_WITH_SECURITY
void write_set_cfg_body(AsyncWebServerRequest* request, uint8_t*, size_t, size_t index, size_t)
{
    if (request && index == 0)
    {
        request->setAttribute(kSetCfgStartedAttribute, true);
        set_cfg_error(request, 501, "Config writes require BUILD_WITH_SECURITY");
    }
}

void finish_set_cfg(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    if (request->hasAttribute(kSetCfgErrorAttribute))
    {
        send_set_cfg_error(request);
        return;
    }

    request->send(501, "text/plain", "Config writes require BUILD_WITH_SECURITY");
}
#else
struct SetCfgUploadState
{
    AsyncWebServerRequest* request = nullptr;
    uint8_t* encrypted = nullptr;
    size_t expected_size = 0;
    size_t received_size = 0;
};

SetCfgUploadState g_set_cfg_upload;

uint8_t* allocate_large_buffer(size_t size)
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

void reset_set_cfg_upload(AsyncWebServerRequest* request = nullptr)
{
    if (request && g_set_cfg_upload.request != request)
    {
        return;
    }

    free(g_set_cfg_upload.encrypted);
    g_set_cfg_upload = {};
}

void set_field_error(String& error, const char* field, const char* problem)
{
    error = field ? field : "field";
    error += " ";
    error += problem ? problem : "is invalid";
}

bool copy_text_field(char* dst, size_t dst_size, const char* value, bool required, String& error, const char* field)
{
    if (!dst || dst_size == 0)
    {
        error = "Internal config storage is invalid";
        return false;
    }

    if (!value)
    {
        value = "";
    }
    if (required && value[0] == '\0')
    {
        set_field_error(error, field, "is required");
        return false;
    }
    if (strlcpy(dst, value, dst_size) >= dst_size)
    {
        set_field_error(error, field, "is too long");
        return false;
    }
    return true;
}

uint8_t parse_json_icon(JsonVariant value, uint8_t default_icon)
{
    if (!value.isNull() && value.is<int>())
    {
        const int icon = value.as<int>();
        if (icon > ICON_UNKNOWN && icon < ICON_LAST)
        {
            return static_cast<uint8_t>(icon);
        }
        return default_icon;
    }

    const char* icon_text = value.as<const char*>();
    if (icon_text && icon_text[0] != '\0')
    {
        const uint8_t parsed = IconLookup::fromString(icon_text);
        if (parsed > ICON_UNKNOWN && parsed < ICON_LAST)
        {
            return parsed;
        }
    }
    return default_icon;
}

bool valid_wifi_password(const char* password)
{
    const size_t length = password ? strlen(password) : 0;
    return length >= 8 && length < kNetworkConfigPasswordMaxLength;
}

const wifi_item_t* find_wifi_by_ssid(const wifi_item_t* items, size_t count, const char* ssid)
{
    if (!items || !ssid)
    {
        return nullptr;
    }

    for (size_t i = 0; i < count; ++i)
    {
        if (strcmp(items[i].ssid, ssid) == 0)
        {
            return &items[i];
        }
    }
    return nullptr;
}

const cloud_item_t* find_cloud_by_identity(const cloud_item_t* items, size_t count, const char* name, const char* url)
{
    if (!items || !name || !url)
    {
        return nullptr;
    }

    for (size_t i = 0; i < count; ++i)
    {
        if (strcmp(items[i].name, name) == 0 && strcmp(items[i].url, url) == 0)
        {
            return &items[i];
        }
    }
    return nullptr;
}

bool parse_wifi_config_array(JsonObject network,
                             const char* key,
                             const wifi_item_t* existing,
                             size_t existing_count,
                             wifi_item_t* target,
                             uint8_t& target_count,
                             uint8_t default_icon,
                             String& error)
{
    JsonVariant array_value = network[key];
    if (array_value.isNull())
    {
        return true;
    }

    JsonArray array = array_value.as<JsonArray>();
    if (array.isNull())
    {
        set_field_error(error, key, "must be an array");
        return false;
    }
    if (array.size() > kNetworkConfigMaxEntries)
    {
        set_field_error(error, key, "has too many entries");
        return false;
    }

    memset(target, 0, sizeof(wifi_item_t) * kNetworkConfigMaxEntries);
    target_count = 0;

    for (JsonVariant value : array)
    {
        JsonObject item_json = value.as<JsonObject>();
        if (item_json.isNull())
        {
            set_field_error(error, key, "contains a non-object entry");
            return false;
        }

        wifi_item_t& item = target[target_count];
        const char* ssid = item_json["ssid"].as<const char*>();
        if (!copy_text_field(item.ssid, sizeof(item.ssid), ssid, true, error, "wifi ssid"))
        {
            return false;
        }

        const char* password = item_json["password"].as<const char*>();
        if (password && password[0] != '\0')
        {
            if (!valid_wifi_password(password) ||
                !copy_text_field(item.password, sizeof(item.password), password, true, error, "wifi password"))
            {
                error = "wifi password must be 8-63 characters";
                return false;
            }
        }
        else
        {
            const wifi_item_t* previous = find_wifi_by_ssid(existing, existing_count, item.ssid);
            if (!previous || !valid_wifi_password(previous->password))
            {
                error = "wifi password is required for new or previously-open networks";
                return false;
            }
            strlcpy(item.password, previous->password, sizeof(item.password));
        }

        item.icon = parse_json_icon(item_json["icon"], default_icon);
        item.next_node = nullptr;
        ++target_count;
    }

    return true;
}

bool parse_cloud_config_array(JsonObject network,
                              const network_cfg_t& existing,
                              cloud_item_t* target,
                              uint8_t& target_count,
                              String& error)
{
    JsonVariant array_value = network["cloud_uploads"];
    if (array_value.isNull())
    {
        return true;
    }

    JsonArray array = array_value.as<JsonArray>();
    if (array.isNull())
    {
        error = "cloud_uploads must be an array";
        return false;
    }
    if (array.size() > kNetworkConfigMaxEntries)
    {
        error = "cloud_uploads has too many entries";
        return false;
    }

    memset(target, 0, sizeof(cloud_item_t) * kNetworkConfigMaxEntries);
    target_count = 0;

    for (JsonVariant value : array)
    {
        JsonObject item_json = value.as<JsonObject>();
        if (item_json.isNull())
        {
            error = "cloud_uploads contains a non-object entry";
            return false;
        }

        cloud_item_t& item = target[target_count];
        const char* name = item_json["name"].as<const char*>();
        const char* url = item_json["url"].as<const char*>();
        if (!copy_text_field(item.name, sizeof(item.name), name, true, error, "cloud name") ||
            !copy_text_field(item.url, sizeof(item.url), url, true, error, "cloud url"))
        {
            return false;
        }

        const char* password = item_json["password"].as<const char*>();
        if (password && password[0] != '\0')
        {
            if (!copy_text_field(item.password, sizeof(item.password), password, false, error, "cloud password"))
            {
                return false;
            }
        }
        else
        {
            const cloud_item_t* previous = find_cloud_by_identity(existing.cloud, existing.cloud_endpoint_count, item.name, item.url);
            if (previous)
            {
                strlcpy(item.password, previous->password, sizeof(item.password));
            }
        }

        item.icon = parse_json_icon(item_json["icon"], ICON_CLOUD);
        item.next_node = nullptr;
        ++target_count;
    }

    return true;
}

bool validate_wifi_config_list(const wifi_item_t* items, size_t count, const char* label, String& error)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (!items[i].ssid[0])
        {
            error = label;
            error += " contains a blank ssid";
            return false;
        }
        if (!valid_wifi_password(items[i].password))
        {
            error = label;
            error += " contains an open or invalid-password network";
            return false;
        }
    }
    return true;
}

bool parse_network_object(JsonObject network, const network_cfg_t& existing, network_cfg_t& staged, String& error)
{
    if (network.isNull())
    {
        error = "network must be an object";
        return false;
    }

    JsonVariant timezone = network["timezone"];
    if (!timezone.isNull() &&
        !copy_text_field(staged.timezone, sizeof(staged.timezone), timezone.as<const char*>(), true, error, "timezone"))
    {
        return false;
    }

    JsonVariant ntp_value = network["ntp_servers"];
    if (!ntp_value.isNull())
    {
        JsonArray ntp_servers = ntp_value.as<JsonArray>();
        if (ntp_servers.isNull())
        {
            error = "ntp_servers must be an array";
            return false;
        }
        if (ntp_servers.size() > kNetworkConfigNtpServerCount)
        {
            error = "ntp_servers has too many entries";
            return false;
        }

        size_t index = 0;
        for (JsonVariant server : ntp_servers)
        {
            char field_name[32] = {};
            snprintf(field_name, sizeof(field_name), "ntp_servers[%u]", static_cast<unsigned>(index));
            if (!copy_text_field(staged.ntp_server[index],
                                 sizeof(staged.ntp_server[index]),
                                 server.as<const char*>(),
                                 true,
                                 error,
                                 field_name))
            {
                return false;
            }
            ++index;
        }
    }

    return parse_wifi_config_array(network,
                                   "stations",
                                   existing.station,
                                   existing.station_count,
                                   staged.station,
                                   staged.station_count,
                                   ICON_WIFI,
                                   error) &&
           parse_wifi_config_array(network,
                                   "access_points",
                                   existing.access_point,
                                   existing.access_point_count,
                                   staged.access_point,
                                   staged.access_point_count,
                                   ICON_WIFIAP,
                                   error) &&
           parse_cloud_config_array(network, existing, staged.cloud, staged.cloud_endpoint_count, error) &&
           validate_wifi_config_list(staged.station, staged.station_count, "stations", error) &&
           validate_wifi_config_list(staged.access_point, staged.access_point_count, "access_points", error);
}

bool mac_is_empty(const esp_bd_addr_t bdaddr)
{
    const esp_bd_addr_t empty = {};
    return bda_equal(bdaddr, empty);
}

const bt_host_item_t* find_bt_host_by_mac(const bt_host_list_t& hosts, const esp_bd_addr_t bdaddr)
{
    for (size_t i = 0; i < hosts.count; ++i)
    {
        if (bda_equal(hosts.host[i].bdaddr, bdaddr))
        {
            return &hosts.host[i];
        }
    }
    return nullptr;
}

bool copy_optional_host_name(JsonObject host,
                             const char* key,
                             char* dst,
                             size_t dst_size,
                             const char* fallback,
                             String& error)
{
    const char* value = fallback ? fallback : "";
    if (!host[key].isNull())
    {
        value = host[key].as<const char*>();
    }
    return copy_text_field(dst, dst_size, value, false, error, key);
}

bool parse_bluetooth_object(JsonObject bluetooth, const bt_host_list_t& existing, bt_host_list_t& staged, String& error)
{
    if (bluetooth.isNull())
    {
        error = "bluetooth must be an object";
        return false;
    }

    JsonVariant hosts_value = bluetooth["hosts"];
    if (hosts_value.isNull())
    {
        return true;
    }

    JsonArray hosts = hosts_value.as<JsonArray>();
    if (hosts.isNull())
    {
        error = "bluetooth hosts must be an array";
        return false;
    }
    if (hosts.size() > kBtHostListMaxEntries)
    {
        error = "bluetooth hosts has too many entries";
        return false;
    }

    memset(&staged, 0, sizeof(staged));
    staged.count = 0;

    for (JsonVariant value : hosts)
    {
        JsonObject host_json = value.as<JsonObject>();
        if (host_json.isNull())
        {
            error = "bluetooth hosts contains a non-object entry";
            return false;
        }

        esp_bd_addr_t bdaddr = {};
        if (!parse_mac(host_json["mac"].as<const char*>(), bdaddr) || mac_is_empty(bdaddr))
        {
            error = "bluetooth host mac is invalid";
            return false;
        }

        const bt_host_item_t* previous = find_bt_host_by_mac(existing, bdaddr);
        bt_host_item_t& item = staged.host[staged.count];
        copy_bda(item.bdaddr, bdaddr);

        const char* custom_fallback = previous ? previous->name_custom : "";
        if (!host_json["name"].isNull() && host_json["name_custom"].isNull())
        {
            custom_fallback = host_json["name"].as<const char*>();
        }
        if (!copy_optional_host_name(host_json, "name_custom", item.name_custom, sizeof(item.name_custom), custom_fallback, error) ||
            !copy_optional_host_name(host_json,
                                     "name_reported",
                                     item.name_reported,
                                     sizeof(item.name_reported),
                                     previous ? previous->name_reported : "",
                                     error))
        {
            return false;
        }

        if (!host_json["last_used"].isNull())
        {
            const long long last_used = host_json["last_used"].as<long long>();
            if (last_used < 0)
            {
                error = "bluetooth host last_used is invalid";
                return false;
            }
            item.last_used = static_cast<time_t>(last_used);
        }
        else if (previous)
        {
            item.last_used = previous->last_used;
        }

        item.bonded = previous ? previous->bonded : false;
        item.icon = parse_json_icon(host_json["icon"], previous ? previous->icon : ICON_BLUETOOTH);
        item.next_node = nullptr;
        ++staged.count;
    }

    return true;
}

bool decrypt_set_cfg_blob(const uint8_t* encrypted,
                          size_t encrypted_size,
                          uint8_t*& plaintext,
                          size_t& plaintext_size,
                          int& status_code,
                          String& error)
{
    plaintext = nullptr;
    plaintext_size = 0;

    if (!encrypted || encrypted_size < kSetCfgMinEnvelopeSize)
    {
        status_code = 400;
        error = "Encrypted config body is too small";
        return false;
    }
    if (memcmp(encrypted, kSetCfgMagic, sizeof(kSetCfgMagic)) != 0)
    {
        status_code = 400;
        error = "Encrypted config magic is invalid";
        return false;
    }
    if (encrypted[sizeof(kSetCfgMagic)] != kSetCfgVersion)
    {
        status_code = 400;
        error = "Encrypted config version is unsupported";
        return false;
    }
    if (!Aegis::isInitialized() && !Aegis::init())
    {
        status_code = 500;
        error = "Master key unavailable";
        return false;
    }

    const uint8_t* master_key = Aegis::getMasterKey();
    if (!master_key)
    {
        status_code = 500;
        error = "Master key unavailable";
        return false;
    }

    const uint8_t* nonce = encrypted + sizeof(kSetCfgMagic) + 1;
    const uint8_t* ciphertext = encrypted + kSetCfgHeaderSize;
    const uint8_t* tag = encrypted + encrypted_size - kGcmTagSize;
    plaintext_size = encrypted_size - kSetCfgHeaderSize - kGcmTagSize;

    plaintext = allocate_large_buffer(plaintext_size + 1);
    if (!plaintext)
    {
        status_code = 500;
        error = "Could not allocate config plaintext buffer";
        return false;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    const int key_result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, master_key, Aegis::kMasterKeySize * 8);
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
        status_code = 400;
        error = "Config decryption failed";
        return false;
    }

    plaintext[plaintext_size] = '\0';
    return true;
}

bool apply_set_cfg_json(const uint8_t* plaintext, size_t plaintext_size, int& status_code, String& error)
{
    JsonDocument doc;
    const DeserializationError json_error =
        deserializeJson(doc, reinterpret_cast<const char*>(plaintext), plaintext_size);
    if (json_error)
    {
        status_code = 400;
        error = "Config JSON parse failed: ";
        error += json_error.c_str();
        return false;
    }

    JsonObject root = doc.as<JsonObject>();
    if (root.isNull())
    {
        status_code = 400;
        error = "Config JSON root must be an object";
        return false;
    }

    const bool has_network = !root["network"].isNull();
    const bool has_bluetooth = !root["bluetooth"].isNull();
    if (!has_network && !has_bluetooth)
    {
        status_code = 400;
        error = "Config JSON must include network or bluetooth";
        return false;
    }

    network_cfg_t network_staged = {};
    bt_host_list_t bluetooth_staged = {};

    if (has_network)
    {
        network_cfg_t network_existing = {};
        if (!wifi_manager || !wifi_manager->copyConfig(network_existing))
        {
            status_code = 500;
            error = "Wi-Fi config is unavailable";
            return false;
        }
        network_staged = network_existing;
        if (!parse_network_object(root["network"].as<JsonObject>(), network_existing, network_staged, error))
        {
            status_code = 400;
            return false;
        }
    }

    if (has_bluetooth)
    {
        if (!bt_host_list || !bt_host_list->copyHostList(bluetooth_staged))
        {
            status_code = 500;
            error = "Bluetooth config is unavailable";
            return false;
        }

        const bt_host_list_t bluetooth_existing = bluetooth_staged;
        if (!parse_bluetooth_object(root["bluetooth"].as<JsonObject>(), bluetooth_existing, bluetooth_staged, error))
        {
            status_code = 400;
            return false;
        }
    }

    if (has_network && !wifi_manager->replaceConfig(network_staged))
    {
        status_code = 500;
        error = "Wi-Fi config save failed";
        return false;
    }
    if (has_bluetooth && !bt_host_list->replaceHostList(bluetooth_staged))
    {
        status_code = 500;
        error = "Bluetooth config save failed";
        return false;
    }

    status_code = 200;
    return true;
}

bool process_set_cfg_blob(const uint8_t* encrypted, size_t encrypted_size, int& status_code, String& error)
{
    uint8_t* plaintext = nullptr;
    size_t plaintext_size = 0;
    if (!decrypt_set_cfg_blob(encrypted, encrypted_size, plaintext, plaintext_size, status_code, error))
    {
        return false;
    }

    const bool ok = apply_set_cfg_json(plaintext, plaintext_size, status_code, error);
    free(plaintext);
    return ok;
}

bool begin_set_cfg_upload(AsyncWebServerRequest* request, size_t total)
{
    request->setAttribute(kSetCfgStartedAttribute, true);
    request->onDisconnect([]() {
        reset_set_cfg_upload();
    });

    const RequestAuthResult auth = authenticate_request(request);
    if (auth != RequestAuthResult::Ok)
    {
        set_cfg_error(request, 401, request_auth_result_name(auth));
        return false;
    }

    const size_t expected_size = total != 0 ? total : request->contentLength();
    if (expected_size == 0)
    {
        set_cfg_error(request, 400, "Missing encrypted config body");
        return false;
    }
    if (expected_size > kSetCfgMaxEncryptedSize)
    {
        set_cfg_error(request, 413, "Encrypted config body is too large");
        return false;
    }
    if (expected_size < kSetCfgMinEnvelopeSize)
    {
        set_cfg_error(request, 400, "Encrypted config body is too small");
        return false;
    }
    if (g_set_cfg_upload.request && g_set_cfg_upload.request != request)
    {
        set_cfg_error(request, 409, "Another config update is already in progress");
        return false;
    }

    reset_set_cfg_upload(request);
    g_set_cfg_upload.encrypted = allocate_large_buffer(expected_size);
    if (!g_set_cfg_upload.encrypted)
    {
        set_cfg_error(request, 500, "Could not allocate encrypted config buffer");
        return false;
    }

    g_set_cfg_upload.request = request;
    g_set_cfg_upload.expected_size = expected_size;
    g_set_cfg_upload.received_size = 0;
    return true;
}

void write_set_cfg_body(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total)
{
    if (!request || len == 0)
    {
        return;
    }

    if (index == 0 && !begin_set_cfg_upload(request, total))
    {
        return;
    }
    if (request->hasAttribute(kSetCfgErrorAttribute))
    {
        return;
    }
    if (g_set_cfg_upload.request != request || !g_set_cfg_upload.encrypted)
    {
        set_cfg_error(request, 500, "Config upload state lost");
        return;
    }
    if (index + len > g_set_cfg_upload.expected_size)
    {
        set_cfg_error(request, 400, "Encrypted config body length mismatch");
        reset_set_cfg_upload(request);
        return;
    }

    memcpy(g_set_cfg_upload.encrypted + index, data, len);
    const size_t received_end = index + len;
    if (received_end > g_set_cfg_upload.received_size)
    {
        g_set_cfg_upload.received_size = received_end;
    }
}

void finish_set_cfg(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    if (request->hasAttribute(kSetCfgErrorAttribute))
    {
        reset_set_cfg_upload(request);
        send_set_cfg_error(request);
        return;
    }
    if (!request->hasAttribute(kSetCfgStartedAttribute))
    {
        set_cfg_error(request, 400, "Missing encrypted config body");
        send_set_cfg_error(request);
        return;
    }
    if (g_set_cfg_upload.request != request || !g_set_cfg_upload.encrypted)
    {
        set_cfg_error(request, 500, "Config upload state lost");
        send_set_cfg_error(request);
        return;
    }
    if (g_set_cfg_upload.received_size != g_set_cfg_upload.expected_size)
    {
        set_cfg_error(request, 400, "Encrypted config body is incomplete");
        reset_set_cfg_upload(request);
        send_set_cfg_error(request);
        return;
    }

    int status_code = 500;
    String error;
    const bool ok = process_set_cfg_blob(g_set_cfg_upload.encrypted,
                                         g_set_cfg_upload.expected_size,
                                         status_code,
                                         error);
    reset_set_cfg_upload(request);

    if (!ok)
    {
        request->send(status_code, "text/plain", error.isEmpty() ? "Config update failed" : error);
        return;
    }

    ESP_LOGI(TAG, "updated config from encrypted web upload");
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}
#endif

class EncryptedConfigJsonStream
{
public:
    EncryptedConfigJsonStream()
    {
        mbedtls_gcm_init(&m_gcm);
        for (size_t i = 0; i < 8; ++i)
        {
            m_nonce_prefix[i] = static_cast<uint8_t>(Aegis::rand() >> ((i % 4) * 8));
        }
    }

    ~EncryptedConfigJsonStream()
    {
        mbedtls_gcm_free(&m_gcm);
    }

    bool begin()
    {
        const uint8_t* master_key = Aegis::getMasterKey();
        if (!master_key)
        {
            return false;
        }

        return mbedtls_gcm_setkey(&m_gcm, MBEDTLS_CIPHER_ID_AES, master_key, Aegis::kMasterKeySize * 8) == 0;
    }

    size_t fill(uint8_t* buffer, size_t max_len)
    {
        if (!buffer || max_len <= kGcmRecordHeaderSize || m_done || m_failed)
        {
            return 0;
        }

        const size_t plain_capacity = min(static_cast<size_t>(kCfgPlainChunkMaxSize), max_len - kGcmRecordHeaderSize);
        const size_t plain_len = fillPlaintext(buffer + 2 + kGcmNonceSize, plain_capacity);
        if (plain_len == 0)
        {
            return 0;
        }

        uint8_t nonce[kGcmNonceSize] = {};
        memcpy(nonce, m_nonce_prefix, sizeof(m_nonce_prefix));
        store_u32_be(nonce + sizeof(m_nonce_prefix), m_record_counter++);

        uint8_t tag[kGcmTagSize] = {};
        if (mbedtls_gcm_crypt_and_tag(&m_gcm,
                                      MBEDTLS_GCM_ENCRYPT,
                                      plain_len,
                                      nonce,
                                      sizeof(nonce),
                                      nullptr,
                                      0,
                                      buffer + 2 + kGcmNonceSize,
                                      buffer + 2 + kGcmNonceSize,
                                      sizeof(tag),
                                      tag) != 0)
        {
            m_failed = true;
            return 0;
        }

        store_u16_be(buffer, static_cast<uint16_t>(plain_len));
        memcpy(buffer + 2, nonce, sizeof(nonce));
        memcpy(buffer + 2 + kGcmNonceSize + plain_len, tag, sizeof(tag));
        return 2 + kGcmNonceSize + plain_len + sizeof(tag);
    }

private:
    enum class Stage
    {
        Start,
        NtpItems,
        StationsStart,
        StationItems,
        AccessPointsStart,
        AccessPointItems,
        CloudStart,
        CloudItems,
        BluetoothStart,
        BluetoothHostItems,
        End,
        Done,
    };

    size_t fillPlaintext(uint8_t* buffer, size_t max_len)
    {
        size_t written = 0;
        while (written < max_len)
        {
            if (m_pending_offset >= m_pending.length())
            {
                if (!prepareNextFragment())
                {
                    break;
                }
            }

            const size_t available = m_pending.length() - m_pending_offset;
            const size_t room      = max_len - written;
            const size_t to_copy   = available < room ? available : room;
            if (to_copy == 0)
            {
                break;
            }

            memcpy(buffer + written, m_pending.c_str() + m_pending_offset, to_copy);
            written += to_copy;
            m_pending_offset += to_copy;
        }

        return written;
    }

    bool prepareNextFragment()
    {
        m_pending = "";
        m_pending_offset = 0;

        switch (m_stage)
        {
        case Stage::Start:
            m_index = 0;
            m_pending = "{\"network\":{\"timezone\":";
            m_pending += json_string(wifi_manager ? wifi_manager->timezone() : "");
            m_pending += ",\"ntp_servers\":[";
            m_stage = Stage::NtpItems;
            return true;

        case Stage::NtpItems:
            if (wifi_manager && m_index < WifiManager::kNtpServerCount)
            {
                if (m_index > 0)
                {
                    m_pending += ",";
                }
                m_pending += json_string(wifi_manager->ntpServer(m_index));
                ++m_index;
                return true;
            }
            m_index = 0;
            m_stage = Stage::StationsStart;
            m_pending = "],\"stations\":[";
            return true;

        case Stage::StationsStart:
            m_stage = Stage::StationItems;
            return prepareNextFragment();

        case Stage::StationItems:
            if (wifi_manager && m_index < wifi_manager->stationCount())
            {
                const wifi_item_t* item = wifi_manager->station(m_index);
                appendWifiItem(item);
                ++m_index;
                return true;
            }
            m_index = 0;
            m_stage = Stage::AccessPointsStart;
            m_pending = "],\"access_points\":[";
            return true;

        case Stage::AccessPointsStart:
            m_stage = Stage::AccessPointItems;
            return prepareNextFragment();

        case Stage::AccessPointItems:
            if (wifi_manager && m_index < wifi_manager->accessPointCount())
            {
                const wifi_item_t* item = wifi_manager->accessPoint(m_index);
                appendWifiItem(item);
                ++m_index;
                return true;
            }
            m_index = 0;
            m_stage = Stage::CloudStart;
            m_pending = "],\"cloud_uploads\":[";
            return true;

        case Stage::CloudStart:
            m_stage = Stage::CloudItems;
            return prepareNextFragment();

        case Stage::CloudItems:
            if (wifi_manager && m_index < wifi_manager->cloudEndpointCount())
            {
                const cloud_item_t* item = wifi_manager->cloudEndpoint(m_index);
                appendCloudItem(item);
                ++m_index;
                return true;
            }
            m_index = 0;
            m_stage = Stage::BluetoothStart;
            m_pending = "]},\"bluetooth\":{\"hosts\":[";
            return true;

        case Stage::BluetoothStart:
            m_stage = Stage::BluetoothHostItems;
            return prepareNextFragment();

        case Stage::BluetoothHostItems:
            if (bt_host_list && m_index < bt_host_list->size())
            {
                const bt_host_item_t* item = bt_host_list->get(m_index);
                appendBluetoothHost(item);
                ++m_index;
                return true;
            }
            m_stage = Stage::End;
            m_pending = "]}}";
            return true;

        case Stage::End:
            m_stage = Stage::Done;
            m_done = true;
            return false;

        case Stage::Done:
        default:
            m_done = true;
            return false;
        }
    }

    void appendWifiItem(const wifi_item_t* item)
    {
        if (m_index > 0)
        {
            m_pending += ",";
        }

        m_pending += "{\"ssid\":";
        #ifndef BUILD_WITH_SECURITY
        m_pending += json_string(item && item->ssid ? item->ssid : "");
        #else
        m_pending += json_string(item ? item->ssid : "");
        #endif
        m_pending += ",\"icon\":";
        m_pending += static_cast<unsigned>(item ? item->icon : 0);
        m_pending += "}";
    }

    void appendCloudItem(const cloud_item_t* item)
    {
        if (m_index > 0)
        {
            m_pending += ",";
        }

        m_pending += "{\"name\":";
        #ifndef BUILD_WITH_SECURITY
        m_pending += json_string(item && item->name ? item->name : "");
        m_pending += ",\"url\":";
        m_pending += json_string(item && item->url ? item->url : "");
        #else
        m_pending += json_string(item ? item->name : "");
        m_pending += ",\"url\":";
        m_pending += json_string(item ? item->url : "");
        #endif
        m_pending += ",\"icon\":";
        m_pending += static_cast<unsigned>(item ? item->icon : 0);
        m_pending += "}";
    }

    void appendBluetoothHost(const bt_host_item_t* item)
    {
        if (m_index > 0)
        {
            m_pending += ",";
        }

        char mac[18] = {};
        if (item)
        {
            format_mac(item->bdaddr, mac, sizeof(mac));
        }

        m_pending += "{\"mac\":";
        m_pending += json_string(mac);
        #ifndef BUILD_WITH_SECURITY
        m_pending += ",\"name\":";
        m_pending += json_string(item && item->name ? item->name : "");
        #else
        m_pending += ",\"name_custom\":";
        m_pending += json_string(item ? item->name_custom : "");
        m_pending += ",\"name_reported\":";
        m_pending += json_string(item ? item->name_reported : "");
        m_pending += ",\"last_used\":";
        m_pending += static_cast<long long>(item ? item->last_used : 0);
        #endif
        m_pending += ",\"bonded\":";
        m_pending += (item && item->bonded) ? "true" : "false";
        m_pending += ",\"icon\":";
        m_pending += static_cast<unsigned>(item ? item->icon : 0);
        m_pending += "}";
    }

    mbedtls_gcm_context m_gcm;
    uint8_t             m_nonce_prefix[8] = {};
    uint32_t            m_record_counter = 0;
    Stage               m_stage = Stage::Start;
    size_t              m_index = 0;
    String              m_pending;
    size_t              m_pending_offset = 0;
    bool                m_done = false;
    bool                m_failed = false;
};

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

void send_cfg(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    const RequestAuthResult auth = authenticate_request(request);
    if (auth != RequestAuthResult::Ok)
    {
        request->send(401, "text/plain", request_auth_result_name(auth));
        return;
    }

    std::shared_ptr<EncryptedConfigJsonStream> stream(new EncryptedConfigJsonStream());
    if (!stream || !stream->begin())
    {
        request->send(500, "text/plain", "Config encryption failed");
        return;
    }

    AsyncWebServerResponse* response = request->beginChunkedResponse(
        "application/octet-stream",
        [stream](uint8_t* buffer, size_t max_len, size_t) -> size_t {
            return stream ? stream->fill(buffer, max_len) : 0;
        });
    if (!response)
    {
        request->send(500);
        return;
    }

    response->addHeader("X-TheFly-Content", "config-json");
    response->addHeader("X-TheFly-Encryption", "aes-256-gcm-records-v1");
    response->addHeader("X-TheFly-Record-Format", "u16be-ciphertext-len|nonce12|ciphertext|tag16");
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

    g_server.on(AsyncURIMatcher::exact("/get_cfg"), HTTP_GET, send_cfg);
    ESP_LOGI(TAG, "registered authenticated config GET /get_cfg");

    g_server.on(AsyncURIMatcher::exact("/set_cfg"), HTTP_POST, finish_set_cfg, nullptr, write_set_cfg_body);
    ESP_LOGI(TAG, "registered authenticated encrypted config POST /set_cfg");

    // Authenticates timestamp/hash metadata, but the uploaded HTTP body is plaintext.
    g_server.on(AsyncURIMatcher::exact("/file_upload"), HTTP_POST, finish_file_upload, write_file_upload_part, write_file_upload_body);
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
