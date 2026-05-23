#include "WebServer.h"

#include <ESPAsyncWebServer.h>

#include <ctype.h>
#include <memory>
#include <string.h>

#include "Aegis.h"
#include "BtHostList.h"
#include "ClockAgent.h"
#ifdef BUILD_FTP_SERVER
#include "FtpServer.h"
#endif
#include "MicroSdCard.h"
#include "WifiManager.h"
#include "esp_log.h"
#include "mbedtls/gcm.h"
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
