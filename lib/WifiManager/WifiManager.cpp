#include "WifiManager.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "AudioFileRecorder.h"
#include "AudioManager.h"
#include "BluetoothManager.h"
#ifdef BUILD_FTP_SERVER
#include "FtpServer.h"
#endif
#include "IconLookup.h"
#include "MicroSdCard.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#ifdef BUILD_WITH_SECURITY
#include "nvs.h"
#endif
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG              = "WifiManager";
constexpr size_t      kMaxJsonFileSize = 16 * 1024;

constexpr const char* kDefaultTimezone = "UTC0";
constexpr const char* kDefaultNtpServers[] = {
    "pool.ntp.org",
    "time.nist.gov",
    "time.google.com",
};
#if defined(CORE_DEBUG_LEVEL)
constexpr int kWifiCoreDebugLevel = CORE_DEBUG_LEVEL;
#else
constexpr int kWifiCoreDebugLevel = static_cast<int>(ESP_LOG_NONE);
#endif

constexpr bool kWifiDebugBuild =
    static_cast<int>(LOG_LOCAL_LEVEL) > static_cast<int>(ESP_LOG_ERROR) ||
    kWifiCoreDebugLevel > static_cast<int>(ESP_LOG_ERROR);
constexpr int kSoftApChannel       = 1;
constexpr int kSoftApSsidHidden    = 0;
constexpr int kSoftApMaxConnection = 1;
#ifdef BUILD_WITH_SECURITY
constexpr uint32_t    kNetworkConfigMagic   = 0x54465749; // "TFWI"
constexpr uint32_t    kNetworkConfigVersion = 1;
constexpr const char* kNetworkNvsNamespace  = "wifi_cfg";
constexpr const char* kNetworkNvsBlobName   = "network";
#endif

const char* load_result_tostring(WifiManager::LoadResult result)
{
    switch (result)
    {
    case WifiManager::LoadResult::Ok:
        return "Ok";
    case WifiManager::LoadResult::SdNotReady:
        return "SdNotReady";
    case WifiManager::LoadResult::FileOpenFailed:
        return "FileOpenFailed";
    case WifiManager::LoadResult::FileTooLarge:
        return "FileTooLarge";
    case WifiManager::LoadResult::FileReadFailed:
        return "FileReadFailed";
    case WifiManager::LoadResult::FileWriteFailed:
        return "FileWriteFailed";
    case WifiManager::LoadResult::JsonParseFailed:
        return "JsonParseFailed";
    case WifiManager::LoadResult::AllocationFailed:
        return "AllocationFailed";
    case WifiManager::LoadResult::InvalidItem:
        return "InvalidItem";
    default:
        return "Unknown";
    }
}

const char* status_name(WifiManager::Status status)
{
    switch (status)
    {
    case WifiManager::Status::Idle:
        return "Idle";
    case WifiManager::Status::StationScanning:
        return "StationScanning";
    case WifiManager::Status::StationConnecting:
        return "StationConnecting";
    case WifiManager::Status::StationConnected:
        return "StationConnected";
    case WifiManager::Status::AccessPoint:
        return "AccessPoint";
    case WifiManager::Status::NoKnownNetwork:
        return "NoKnownNetwork";
    case WifiManager::Status::ScanFailed:
        return "ScanFailed";
    case WifiManager::Status::ConnectFailed:
        return "ConnectFailed";
    case WifiManager::Status::AccessPointFailed:
        return "AccessPointFailed";
    default:
        return "Unknown";
    }
}

bool replace_string(char*& dst, const char* value)
{
    char* replacement = clone_string(value);
    if (!replacement)
    {
        return false;
    }

    free(dst);
    dst = replacement;
    return true;
}

uint8_t parse_icon_or_default(const char* icon, uint8_t default_icon)
{
    if (!icon || icon[0] == '\0')
    {
        return default_icon;
    }

    const uint8_t parsed = IconLookup::fromString(icon);
    return parsed == ICON_UNKNOWN ? default_icon : parsed;
}

#ifndef BUILD_WITH_SECURITY

void free_wifi_list(wifi_item_t*& head, wifi_item_t*& tail, size_t& count)
{
    wifi_item_t* item = head;
    while (item)
    {
        wifi_item_t* next = static_cast<wifi_item_t*>(item->next_node);
        free(item->ssid);
        free(item->password);
        free(item);
        item = next;
    }

    head  = nullptr;
    tail  = nullptr;
    count = 0;
}

void free_cloud_list(cloud_item_t*& head, cloud_item_t*& tail, size_t& count)
{
    cloud_item_t* item = head;
    while (item)
    {
        cloud_item_t* next = static_cast<cloud_item_t*>(item->next_node);
        free(item->name);
        free(item->url);
        free(item->password);
        free(item);
        item = next;
    }

    head  = nullptr;
    tail  = nullptr;
    count = 0;
}

wifi_item_t* create_wifi_item(const char* ssid, const char* password, uint8_t icon)
{
    wifi_item_t* item = static_cast<wifi_item_t*>(calloc(1, sizeof(wifi_item_t)));
    if (!item)
    {
        return nullptr;
    }

    item->ssid = clone_string(ssid);
    item->password = clone_string(password);
    if (!item->ssid || !item->password)
    {
        free(item->ssid);
        free(item->password);
        free(item);
        return nullptr;
    }

    item->icon      = icon;
    item->next_node = nullptr;
    return item;
}

cloud_item_t* create_cloud_item(const char* name, const char* url, const char* password, uint8_t icon)
{
    cloud_item_t* item = static_cast<cloud_item_t*>(calloc(1, sizeof(cloud_item_t)));
    if (!item)
    {
        return nullptr;
    }

    item->name     = clone_string(name);
    item->url      = clone_string(url);
    item->password = clone_string(password);
    if (!item->name || !item->url || !item->password)
    {
        free(item->name);
        free(item->url);
        free(item->password);
        free(item);
        return nullptr;
    }

    item->icon      = icon;
    item->next_node = nullptr;
    return item;
}

void append_wifi_item(wifi_item_t*& head, wifi_item_t*& tail, size_t& count, wifi_item_t* item)
{
    if (tail)
    {
        tail->next_node = item;
    }
    else
    {
        head = item;
    }

    tail = item;
    ++count;
}

void append_cloud_item(cloud_item_t*& head, cloud_item_t*& tail, size_t& count, cloud_item_t* item)
{
    if (tail)
    {
        tail->next_node = item;
    }
    else
    {
        head = item;
    }

    tail = item;
    ++count;
}

#else

bool copy_config_text(char* dst, size_t dst_size, const char* value, bool required)
{
    if (!dst || dst_size == 0)
    {
        return false;
    }

    if (!value)
    {
        value = "";
    }
    if (required && value[0] == '\0')
    {
        return false;
    }

    return strlcpy(dst, value, dst_size) < dst_size;
}

void init_network_config_defaults(network_cfg_t& cfg)
{
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic   = kNetworkConfigMagic;
    cfg.version = kNetworkConfigVersion;
    strlcpy(cfg.timezone, kDefaultTimezone, sizeof(cfg.timezone));
    for (size_t i = 0; i < kNetworkConfigNtpServerCount; ++i)
    {
        strlcpy(cfg.ntp_server[i], kDefaultNtpServers[i], sizeof(cfg.ntp_server[i]));
    }
}

void sanitize_network_config(network_cfg_t& cfg)
{
    cfg.magic   = kNetworkConfigMagic;
    cfg.version = kNetworkConfigVersion;
    cfg.timezone[sizeof(cfg.timezone) - 1] = '\0';
    if (cfg.timezone[0] == '\0')
    {
        strlcpy(cfg.timezone, kDefaultTimezone, sizeof(cfg.timezone));
    }

    for (size_t i = 0; i < kNetworkConfigNtpServerCount; ++i)
    {
        cfg.ntp_server[i][sizeof(cfg.ntp_server[i]) - 1] = '\0';
        if (cfg.ntp_server[i][0] == '\0')
        {
            strlcpy(cfg.ntp_server[i], kDefaultNtpServers[i], sizeof(cfg.ntp_server[i]));
        }
    }

    cfg.station_count = cfg.station_count > kNetworkConfigMaxEntries ? static_cast<uint8_t>(kNetworkConfigMaxEntries) : cfg.station_count;
    cfg.access_point_count = cfg.access_point_count > kNetworkConfigMaxEntries ? static_cast<uint8_t>(kNetworkConfigMaxEntries) : cfg.access_point_count;
    cfg.cloud_endpoint_count = cfg.cloud_endpoint_count > kNetworkConfigMaxEntries ? static_cast<uint8_t>(kNetworkConfigMaxEntries) : cfg.cloud_endpoint_count;

    for (size_t i = 0; i < kNetworkConfigMaxEntries; ++i)
    {
        cfg.station[i].ssid[sizeof(cfg.station[i].ssid) - 1] = '\0';
        cfg.station[i].password[sizeof(cfg.station[i].password) - 1] = '\0';
        cfg.station[i].next_node = nullptr;

        cfg.access_point[i].ssid[sizeof(cfg.access_point[i].ssid) - 1] = '\0';
        cfg.access_point[i].password[sizeof(cfg.access_point[i].password) - 1] = '\0';
        cfg.access_point[i].next_node = nullptr;

        cfg.cloud[i].name[sizeof(cfg.cloud[i].name) - 1] = '\0';
        cfg.cloud[i].url[sizeof(cfg.cloud[i].url) - 1] = '\0';
        cfg.cloud[i].password[sizeof(cfg.cloud[i].password) - 1] = '\0';
        cfg.cloud[i].next_node = nullptr;
    }
}

bool parse_network_wifi_array(JsonDocument& doc,
                              const char* key,
                              wifi_item_t* items,
                              uint8_t& count,
                              uint8_t default_icon,
                              size_t& skipped)
{
    count = 0;
    JsonArray array = doc[key].as<JsonArray>();
    if (array.isNull())
    {
        return true;
    }

    for (JsonVariant value : array)
    {
        if (count >= kNetworkConfigMaxEntries)
        {
            ++skipped;
            continue;
        }

        JsonObject item_json = value.as<JsonObject>();
        const char* ssid     = item_json["ssid"].as<const char*>();
        const char* password = item_json["password"].as<const char*>();
        const char* icon     = item_json["icon"].as<const char*>();
        if (item_json.isNull() ||
            !copy_config_text(items[count].ssid, sizeof(items[count].ssid), ssid, true) ||
            !copy_config_text(items[count].password, sizeof(items[count].password), password, false))
        {
            ++skipped;
            continue;
        }

        items[count].icon      = parse_icon_or_default(icon, default_icon);
        items[count].next_node = nullptr;
        ++count;
    }

    return true;
}

bool parse_network_cloud_array(JsonDocument& doc, cloud_item_t* items, uint8_t& count, size_t& skipped)
{
    count = 0;
    JsonArray array = doc["cloud_uploads"].as<JsonArray>();
    if (array.isNull())
    {
        return true;
    }

    for (JsonVariant value : array)
    {
        if (count >= kNetworkConfigMaxEntries)
        {
            ++skipped;
            continue;
        }

        JsonObject item_json = value.as<JsonObject>();
        const char* name     = item_json["name"].as<const char*>();
        const char* url      = item_json["url"].as<const char*>();
        const char* password = item_json["password"].as<const char*>();
        const char* icon     = item_json["icon"].as<const char*>();
        if (item_json.isNull() ||
            !copy_config_text(items[count].name, sizeof(items[count].name), name, true) ||
            !copy_config_text(items[count].url, sizeof(items[count].url), url, true) ||
            !copy_config_text(items[count].password, sizeof(items[count].password), password, false))
        {
            ++skipped;
            continue;
        }

        items[count].icon      = parse_icon_or_default(icon, ICON_CLOUD);
        items[count].next_node = nullptr;
        ++count;
    }

    return true;
}

bool parse_network_config_json(JsonDocument& doc, network_cfg_t& cfg, size_t& skipped)
{
    init_network_config_defaults(cfg);
    skipped = 0;

    const char* timezone = doc["timezone"].as<const char*>();
    if (timezone && !copy_config_text(cfg.timezone, sizeof(cfg.timezone), timezone, true))
    {
        ++skipped;
    }

    JsonArray ntp_servers = doc["ntp_servers"].as<JsonArray>();
    if (!ntp_servers.isNull())
    {
        size_t index = 0;
        for (JsonVariant value : ntp_servers)
        {
            if (index >= kNetworkConfigNtpServerCount)
            {
                ++skipped;
                continue;
            }

            const char* server = value.as<const char*>();
            if (!copy_config_text(cfg.ntp_server[index], sizeof(cfg.ntp_server[index]), server, true))
            {
                ++skipped;
                continue;
            }
            ++index;
        }
    }

    parse_network_wifi_array(doc, "stations", cfg.station, cfg.station_count, ICON_WIFI, skipped);
    parse_network_wifi_array(doc, "access_points", cfg.access_point, cfg.access_point_count, ICON_WIFIAP, skipped);
    parse_network_cloud_array(doc, cfg.cloud, cfg.cloud_endpoint_count, skipped);
    sanitize_network_config(cfg);
    return skipped == 0;
}

#endif

bool scan_has_ssid(int network_count, const char* ssid)
{
    if (!ssid)
    {
        return false;
    }

    for (int i = 0; i < network_count; ++i)
    {
        if (WiFi.SSID(i) == ssid)
        {
            return true;
        }
    }

    return false;
}

bool is_connected_status(WifiManager::Status status)
{
    return status == WifiManager::Status::StationConnected || status == WifiManager::Status::AccessPoint;
}

void shutdown_for_wifi_activation()
{
    const BtManager::Result bt_result = BtManager::shutdown();
    if (bt_result != BtManager::Result::Ok)
    {
        ESP_LOGW(TAG, "Bluetooth shutdown before Wi-Fi returned: %s", BtManager::resultName(bt_result));
    }

    AudioManager::stop();
    if (!AudioFileRecorder::stopRecording())
    {
        ESP_LOGW(TAG, "audio file recorder did not stop cleanly before Wi-Fi activation");
    }
}

void format_generated_soft_ap_ssid(char* ssid, size_t ssid_size)
{
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_BT) != ESP_OK)
    {
        snprintf(ssid, ssid_size, "The-Fly-0000");
        return;
    }

    snprintf(ssid, ssid_size, "The-Fly-%02X%02X", mac[4], mac[5]);
}

void format_generated_soft_ap_password(char* password, size_t password_size)
{
    if (!password || password_size == 0)
    {
        return;
    }

    if (kWifiDebugBuild)
    {
        strlcpy(password, "12345678", password_size);
        return;
    }

    snprintf(password, password_size, "%08lu", static_cast<unsigned long>(generate_8_digit_nonce()));
}

bool enforce_soft_ap_security()
{
    wifi_config_t config = {};
    esp_err_t err = esp_wifi_get_config(WIFI_IF_AP, &config);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "could not read SoftAP config for security enforcement: %s", esp_err_to_name(err));
        return false;
    }

    config.ap.authmode       = WIFI_AUTH_WPA3_PSK;
    config.ap.max_connection = kSoftApMaxConnection;
    config.ap.pmf_cfg.capable = true;
    config.ap.pmf_cfg.required = true;
#if defined(WIFI_CIPHER_TYPE_CCMP)
    config.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP; // if not set, WPA3 should still require modern cipher behavior
#endif
#if defined(WPA3_SAE_PWE_BOTH)
    config.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH; // if missing, possible client compatibility issue, not obvious insecurity
#endif
    // security review of these optional settings: all 4 configurations are safe

    err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "could not enforce WPA3-only SoftAP config: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

} // namespace

WifiManager::WifiManager()
{
    clear();
}

WifiManager::~WifiManager()
{
    #ifndef BUILD_WITH_SECURITY
    free_wifi_list(m_station_head, m_station_tail, m_station_count);
    free_wifi_list(m_access_point_head, m_access_point_tail, m_access_point_count);
    free_cloud_list(m_cloud_endpoint_head, m_cloud_endpoint_tail, m_cloud_endpoint_count);

    free(m_timezone);
    m_timezone = nullptr;

    for (size_t i = 0; i < kNtpServerCount; ++i)
    {
        free(m_ntp_servers[i]);
        m_ntp_servers[i] = nullptr;
    }
    #endif
}

bool WifiManager::loadFromMicroSd(const char* path)
{
    #ifndef BUILD_WITH_SECURITY
    clear();

    if (!MicroSdCard::isReady())
    {
        m_last_load_result = LoadResult::SdNotReady;
        ESP_LOGW(TAG, "microSD is not ready while loading Wi-Fi config");
        return false;
    }

    FsFile file;
    if (!file.open(path ? path : "/wifi.json", O_RDONLY))
    {
        m_last_load_result = LoadResult::FileOpenFailed;
        ESP_LOGW(TAG, "could not open Wi-Fi config: %s", path ? path : "/wifi.json");
        return false;
    }

    const uint64_t file_size = file.fileSize();
    if (file_size > kMaxJsonFileSize)
    {
        file.close();
        m_last_load_result = LoadResult::FileTooLarge;
        ESP_LOGW(TAG, "Wi-Fi config is too large: %llu bytes", static_cast<unsigned long long>(file_size));
        return false;
    }

    char* buffer = static_cast<char*>(malloc(static_cast<size_t>(file_size) + 1));
    if (!buffer)
    {
        file.close();
        m_last_load_result = LoadResult::AllocationFailed;
        ESP_LOGW(TAG, "could not allocate Wi-Fi config buffer");
        return false;
    }

    const int bytes_read = file.read(buffer, static_cast<size_t>(file_size));
    file.close();

    if (bytes_read < 0 || static_cast<uint64_t>(bytes_read) != file_size)
    {
        free(buffer);
        m_last_load_result = LoadResult::FileReadFailed;
        ESP_LOGW(TAG, "could not read Wi-Fi config");
        return false;
    }
    buffer[file_size] = '\0';

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, buffer);
    free(buffer);

    if (error)
    {
        m_last_load_result = LoadResult::JsonParseFailed;
        ESP_LOGW(TAG, "could not parse Wi-Fi config: %s", error.c_str());
        return false;
    }

    const char* timezone = doc["timezone"].as<const char*>();
    if (timezone && !replace_string(m_timezone, timezone))
    {
        clear();
        m_last_load_result = LoadResult::AllocationFailed;
        ESP_LOGW(TAG, "could not allocate timezone");
        return false;
    }

    JsonArray ntp_servers = doc["ntp_servers"].as<JsonArray>();
    if (!ntp_servers.isNull())
    {
        size_t index = 0;
        for (JsonVariant value : ntp_servers)
        {
            if (index >= kNtpServerCount)
            {
                break;
            }

            const char* server = value.as<const char*>();
            if (server && !replace_string(m_ntp_servers[index], server))
            {
                clear();
                m_last_load_result = LoadResult::AllocationFailed;
                ESP_LOGW(TAG, "could not allocate NTP server");
                return false;
            }
            ++index;
        }
    }

    size_t skipped = 0;

    JsonArray stations = doc["stations"].as<JsonArray>();
    if (!stations.isNull())
    {
        for (JsonVariant value : stations)
        {
            JsonObject item_json = value.as<JsonObject>();
            const char* ssid     = item_json["ssid"].as<const char*>();
            if (item_json.isNull() || !ssid || ssid[0] == '\0')
            {
                ++skipped;
                continue;
            }

            const char* password = item_json["password"].as<const char*>();
            const char* icon     = item_json["icon"].as<const char*>();
            wifi_item_t* item    = create_wifi_item(ssid, password, parse_icon_or_default(icon, ICON_WIFI));
            if (!item)
            {
                clear();
                m_last_load_result = LoadResult::AllocationFailed;
                ESP_LOGW(TAG, "could not allocate Wi-Fi station item");
                return false;
            }

            append_wifi_item(m_station_head, m_station_tail, m_station_count, item);
        }
    }

    JsonArray access_points = doc["access_points"].as<JsonArray>();
    if (!access_points.isNull())
    {
        for (JsonVariant value : access_points)
        {
            JsonObject item_json = value.as<JsonObject>();
            const char* ssid     = item_json["ssid"].as<const char*>();
            if (item_json.isNull() || !ssid || ssid[0] == '\0')
            {
                ++skipped;
                continue;
            }

            const char* password = item_json["password"].as<const char*>();
            const char* icon     = item_json["icon"].as<const char*>();
            wifi_item_t* item    = create_wifi_item(ssid, password, parse_icon_or_default(icon, ICON_WIFIAP));
            if (!item)
            {
                clear();
                m_last_load_result = LoadResult::AllocationFailed;
                ESP_LOGW(TAG, "could not allocate Wi-Fi access point item");
                return false;
            }

            append_wifi_item(m_access_point_head, m_access_point_tail, m_access_point_count, item);
        }
    }

    JsonArray cloud_uploads = doc["cloud_uploads"].as<JsonArray>();
    if (!cloud_uploads.isNull())
    {
        for (JsonVariant value : cloud_uploads)
        {
            JsonObject item_json = value.as<JsonObject>();
            const char* name     = item_json["name"].as<const char*>();
            const char* url      = item_json["url"].as<const char*>();
            if (item_json.isNull() || !name || name[0] == '\0' || !url || url[0] == '\0')
            {
                ++skipped;
                continue;
            }

            const char* password = item_json["password"].as<const char*>();
            const char* icon     = item_json["icon"].as<const char*>();
            cloud_item_t* item   = create_cloud_item(name, url, password, parse_icon_or_default(icon, ICON_CLOUD));
            if (!item)
            {
                clear();
                m_last_load_result = LoadResult::AllocationFailed;
                ESP_LOGW(TAG, "could not allocate cloud endpoint item");
                return false;
            }

            append_cloud_item(m_cloud_endpoint_head, m_cloud_endpoint_tail, m_cloud_endpoint_count, item);
        }
    }

    if (skipped > 0)
    {
        m_last_load_result = LoadResult::InvalidItem;
        ESP_LOGW(TAG, "loaded Wi-Fi config with %u invalid item(s) skipped", static_cast<unsigned>(skipped));
        return false;
    }

    m_last_load_result = LoadResult::Ok;
    ESP_LOGI(TAG,
             "loaded Wi-Fi config: stations=%u access_points=%u cloud_endpoints=%u",
             static_cast<unsigned>(m_station_count),
             static_cast<unsigned>(m_access_point_count),
             static_cast<unsigned>(m_cloud_endpoint_count));
    return true;
    #else
    if (!loadFromNvs())
    {
        return false;
    }

    const char* import_path = path ? path : "/wifi.json";
    if (!MicroSdCard::isReady())
    {
        ESP_LOGI(TAG, "microSD is not ready; keeping Wi-Fi config from NVS");
        return true;
    }

    FsFile file;
    if (!file.open(import_path, O_RDONLY))
    {
        ESP_LOGI(TAG, "no Wi-Fi JSON import found at %s; keeping Wi-Fi config from NVS", import_path);
        return true;
    }

    const uint64_t file_size = file.fileSize();
    if (file_size > kMaxJsonFileSize)
    {
        file.close();
        m_last_load_result = LoadResult::FileTooLarge;
        ESP_LOGW(TAG, "Wi-Fi config import is too large: %llu bytes", static_cast<unsigned long long>(file_size));
        return false;
    }

    char* buffer = static_cast<char*>(malloc(static_cast<size_t>(file_size) + 1));
    if (!buffer)
    {
        file.close();
        m_last_load_result = LoadResult::AllocationFailed;
        ESP_LOGW(TAG, "could not allocate Wi-Fi config import buffer");
        return false;
    }

    const int bytes_read = file.read(buffer, static_cast<size_t>(file_size));
    file.close();

    if (bytes_read < 0 || static_cast<uint64_t>(bytes_read) != file_size)
    {
        free(buffer);
        m_last_load_result = LoadResult::FileReadFailed;
        ESP_LOGW(TAG, "could not read Wi-Fi config import");
        return false;
    }
    buffer[file_size] = '\0';

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, buffer);
    free(buffer);
    if (error)
    {
        m_last_load_result = LoadResult::JsonParseFailed;
        ESP_LOGW(TAG, "could not parse Wi-Fi config import: %s", error.c_str());
        return false;
    }

    network_cfg_t imported = {};
    size_t skipped = 0;
    if (!parse_network_config_json(doc, imported, skipped))
    {
        m_last_load_result = LoadResult::InvalidItem;
        ESP_LOGW(TAG, "Wi-Fi config import has %u invalid item(s)", static_cast<unsigned>(skipped));
        return false;
    }

    m_network_cfg = imported;
    m_station_count = m_network_cfg.station_count;
    m_access_point_count = m_network_cfg.access_point_count;
    m_cloud_endpoint_count = m_network_cfg.cloud_endpoint_count;

    if (!saveToNvs())
    {
        return false;
    }

    m_last_load_result = LoadResult::Ok;
    ESP_LOGI(TAG,
             "imported Wi-Fi config into NVS: stations=%u access_points=%u cloud_endpoints=%u",
             static_cast<unsigned>(m_station_count),
             static_cast<unsigned>(m_access_point_count),
             static_cast<unsigned>(m_cloud_endpoint_count));
    return true;
    #endif
}

#ifdef BUILD_WITH_SECURITY
bool WifiManager::loadFromNvs()
{
    clear();

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNetworkNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        m_last_load_result = LoadResult::FileOpenFailed;
        ESP_LOGW(TAG, "could not open Wi-Fi NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    network_cfg_t cfg = {};
    size_t cfg_size = sizeof(cfg);
    err = nvs_get_blob(handle, kNetworkNvsBlobName, &cfg, &cfg_size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        m_last_load_result = LoadResult::Ok;
        ESP_LOGI(TAG, "no Wi-Fi config in NVS; using defaults");
        return true;
    }
    if (err != ESP_OK || cfg_size != sizeof(cfg))
    {
        m_last_load_result = LoadResult::FileReadFailed;
        ESP_LOGW(TAG, "could not load Wi-Fi config from NVS: %s size=%u", esp_err_to_name(err), static_cast<unsigned>(cfg_size));
        return false;
    }
    if (cfg.magic != kNetworkConfigMagic || cfg.version != kNetworkConfigVersion)
    {
        m_last_load_result = LoadResult::Ok;
        ESP_LOGW(TAG, "ignoring incompatible Wi-Fi config in NVS");
        return true;
    }

    sanitize_network_config(cfg);
    m_network_cfg = cfg;
    m_station_count = m_network_cfg.station_count;
    m_access_point_count = m_network_cfg.access_point_count;
    m_cloud_endpoint_count = m_network_cfg.cloud_endpoint_count;
    m_last_load_result = LoadResult::Ok;
    ESP_LOGI(TAG,
             "loaded Wi-Fi config from NVS: stations=%u access_points=%u cloud_endpoints=%u",
             static_cast<unsigned>(m_station_count),
             static_cast<unsigned>(m_access_point_count),
             static_cast<unsigned>(m_cloud_endpoint_count));
    return true;
}

bool WifiManager::saveToNvs()
{
    sanitize_network_config(m_network_cfg);
    m_network_cfg.station_count = static_cast<uint8_t>(m_station_count);
    m_network_cfg.access_point_count = static_cast<uint8_t>(m_access_point_count);
    m_network_cfg.cloud_endpoint_count = static_cast<uint8_t>(m_cloud_endpoint_count);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNetworkNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        m_last_load_result = LoadResult::FileOpenFailed;
        ESP_LOGW(TAG, "could not open Wi-Fi NVS namespace for write: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(handle, kNetworkNvsBlobName, &m_network_cfg, sizeof(m_network_cfg));
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK)
    {
        m_last_load_result = LoadResult::FileWriteFailed;
        ESP_LOGW(TAG, "could not save Wi-Fi config to NVS: %s", esp_err_to_name(err));
        return false;
    }

    m_last_load_result = LoadResult::Ok;
    return true;
}

bool WifiManager::copyConfig(network_cfg_t& out) const
{
    out = m_network_cfg;
    sanitize_network_config(out);
    return true;
}

bool WifiManager::replaceConfig(const network_cfg_t& config)
{
    network_cfg_t staged = config;
    sanitize_network_config(staged);

    m_network_cfg = staged;
    m_station_count = m_network_cfg.station_count;
    m_access_point_count = m_network_cfg.access_point_count;
    m_cloud_endpoint_count = m_network_cfg.cloud_endpoint_count;
    m_active_wifi = nullptr;
    m_connected_wifi = nullptr;
    m_reported_connected = false;

    return saveToNvs();
}
#endif

void WifiManager::clear()
{
    #ifndef BUILD_WITH_SECURITY
    free_wifi_list(m_station_head, m_station_tail, m_station_count);
    free_wifi_list(m_access_point_head, m_access_point_tail, m_access_point_count);
    free_cloud_list(m_cloud_endpoint_head, m_cloud_endpoint_tail, m_cloud_endpoint_count);
    m_active_wifi = nullptr;
    m_connected_wifi = nullptr;
    m_reported_connected = false;

    if (!replace_string(m_timezone, kDefaultTimezone))
    {
        m_last_load_result = LoadResult::AllocationFailed;
        ESP_LOGW(TAG, "could not allocate default timezone");
        return;
    }

    for (size_t i = 0; i < kNtpServerCount; ++i)
    {
        if (!replace_string(m_ntp_servers[i], kDefaultNtpServers[i]))
        {
            m_last_load_result = LoadResult::AllocationFailed;
            ESP_LOGW(TAG, "could not allocate default NTP server");
            return;
        }
    }

    m_last_load_result = LoadResult::Ok;
    #else
    init_network_config_defaults(m_network_cfg);
    m_station_count = 0;
    m_access_point_count = 0;
    m_cloud_endpoint_count = 0;
    m_active_wifi = nullptr;
    m_connected_wifi = nullptr;
    m_reported_connected = false;
    m_last_load_result = LoadResult::Ok;
    #endif
}

const char* WifiManager::timezone() const
{
    #ifndef BUILD_WITH_SECURITY
    return m_timezone ? m_timezone : kDefaultTimezone;
    #else
    return m_network_cfg.timezone[0] != '\0' ? m_network_cfg.timezone : kDefaultTimezone;
    #endif
}

const char* WifiManager::ntpServer(size_t index) const
{
    if (index >= kNtpServerCount)
    {
        return nullptr;
    }
    #ifndef BUILD_WITH_SECURITY
    return m_ntp_servers[index] ? m_ntp_servers[index] : kDefaultNtpServers[index];
    #else
    return m_network_cfg.ntp_server[index][0] != '\0' ? m_network_cfg.ntp_server[index] : kDefaultNtpServers[index];
    #endif
}

size_t WifiManager::stationCount() const
{
    return m_station_count;
}

wifi_item_t* WifiManager::station(size_t index)
{
    return const_cast<wifi_item_t*>(static_cast<const WifiManager*>(this)->station(index));
}

const wifi_item_t* WifiManager::station(size_t index) const
{
    #ifndef BUILD_WITH_SECURITY
    const wifi_item_t* item = m_station_head;
    while (item && index > 0)
    {
        item = static_cast<const wifi_item_t*>(item->next_node);
        --index;
    }
    return item;
    #else
    return index < m_station_count ? &m_network_cfg.station[index] : nullptr;
    #endif
}

size_t WifiManager::accessPointCount() const
{
    return m_access_point_count;
}

wifi_item_t* WifiManager::accessPoint(size_t index)
{
    return const_cast<wifi_item_t*>(static_cast<const WifiManager*>(this)->accessPoint(index));
}

const wifi_item_t* WifiManager::accessPoint(size_t index) const
{
    #ifndef BUILD_WITH_SECURITY
    const wifi_item_t* item = m_access_point_head;
    while (item && index > 0)
    {
        item = static_cast<const wifi_item_t*>(item->next_node);
        --index;
    }
    return item;
    #else
    return index < m_access_point_count ? &m_network_cfg.access_point[index] : nullptr;
    #endif
}

bool WifiManager::connectToHotspot(const wifi_item_t* hotspot)
{
    return connectToHotspot(hotspot, true);
}

bool WifiManager::connectToHotspot(const wifi_item_t* hotspot, bool shutdown_first)
{
    if (!hotspot || !hotspot->ssid || hotspot->ssid[0] == '\0')
    {
        m_status      = Status::ConnectFailed;
        m_active_wifi = nullptr;
        ESP_LOGW(TAG, "cannot connect to missing Wi-Fi hotspot");
        return false;
    }

    if (shutdown_first)
    {
        shutdown_for_wifi_activation();
    }

    if (m_reported_connected)
    {
        notifyDisconnected(m_connected_wifi);
    }

    WiFi.softAPdisconnect(true);
    if (!WiFi.mode(WIFI_STA))
    {
        m_status      = Status::ConnectFailed;
        m_active_wifi = nullptr;
        ESP_LOGW(TAG, "could not switch Wi-Fi to station mode");
        return false;
    }

    const char* password = hotspot->password && hotspot->password[0] != '\0' ? hotspot->password : nullptr;
    WiFi.begin(hotspot->ssid, password);

    m_active_wifi = hotspot;
    m_status      = Status::StationConnecting;
    ESP_LOGI(TAG, "started Wi-Fi station connection to \"%s\"", hotspot->ssid);
    return true;
}

bool WifiManager::startSoftAp(const wifi_item_t* access_point)
{
    if (!access_point || !access_point->ssid || access_point->ssid[0] == '\0')
    {
        m_status      = Status::AccessPointFailed;
        m_active_wifi = nullptr;
        ESP_LOGW(TAG, "cannot start missing Wi-Fi access point");
        return false;
    }

    shutdown_for_wifi_activation();

    if (m_reported_connected)
    {
        notifyDisconnected(m_connected_wifi);
    }

    WiFi.disconnect(true, false);
    if (!WiFi.mode(WIFI_AP))
    {
        m_status      = Status::AccessPointFailed;
        m_active_wifi = nullptr;
        ESP_LOGW(TAG, "could not switch Wi-Fi to access point mode");
        return false;
    }

    const char* password = access_point->password && access_point->password[0] != '\0' ? access_point->password : nullptr;
    if (!password || strlen(password) < 8)
    {
        m_status      = Status::AccessPointFailed;
        m_active_wifi = nullptr;
        ESP_LOGW(TAG, "cannot start WPA3-only Wi-Fi access point \"%s\" without an 8+ character password", access_point->ssid);
        return false;
    }

    const bool started = WiFi.softAP(access_point->ssid, password, kSoftApChannel, kSoftApSsidHidden, kSoftApMaxConnection);
    if (!started)
    {
        m_status      = Status::AccessPointFailed;
        m_active_wifi = nullptr;
        ESP_LOGW(TAG, "could not start Wi-Fi access point \"%s\"", access_point->ssid);
        return false;
    }
    if (!enforce_soft_ap_security())
    {
        WiFi.softAPdisconnect(true);
        m_status      = Status::AccessPointFailed;
        m_active_wifi = nullptr;
        ESP_LOGW(TAG, "stopped Wi-Fi access point \"%s\" because WPA3-only security could not be enforced", access_point->ssid);
        return false;
    }

    m_active_wifi = access_point;
    m_status      = Status::AccessPoint;
    notifyConnected(access_point);
    ESP_LOGI(TAG, "started Wi-Fi access point \"%s\" at %s", access_point->ssid, WiFi.softAPIP().toString().c_str());
    return true;
}

bool WifiManager::startGeneratedSoftAp()
{
    format_generated_soft_ap_ssid(m_generated_soft_ap_ssid, sizeof(m_generated_soft_ap_ssid));
    format_generated_soft_ap_password(m_generated_soft_ap_password, sizeof(m_generated_soft_ap_password));

    #ifndef BUILD_WITH_SECURITY
    m_generated_soft_ap.ssid      = m_generated_soft_ap_ssid;
    m_generated_soft_ap.password  = m_generated_soft_ap_password;
    #else
    strlcpy(m_generated_soft_ap.ssid, m_generated_soft_ap_ssid, sizeof(m_generated_soft_ap.ssid));
    strlcpy(m_generated_soft_ap.password, m_generated_soft_ap_password, sizeof(m_generated_soft_ap.password));
    #endif
    m_generated_soft_ap.icon      = ICON_WIFIAP;
    m_generated_soft_ap.next_node = nullptr;
    return startSoftAp(&m_generated_soft_ap);
}

bool WifiManager::scanAndConnect()
{
    if (m_status == Status::StationScanning)
    {
        return true;
    }

    if (m_reported_connected)
    {
        notifyDisconnected(m_connected_wifi);
    }

    if (m_station_count == 0)
    {
        m_status      = Status::NoKnownNetwork;
        m_active_wifi = nullptr;
        ESP_LOGW(TAG, "cannot scan/connect: no configured Wi-Fi stations");
        return false;
    }

    shutdown_for_wifi_activation();

    WiFi.softAPdisconnect(true);
    if (!WiFi.mode(WIFI_STA))
    {
        m_status      = Status::ScanFailed;
        m_active_wifi = nullptr;
        ESP_LOGW(TAG, "could not switch Wi-Fi to station mode for scan");
        return false;
    }

    const int scan_result = WiFi.scanNetworks(true);
    if (scan_result != WIFI_SCAN_RUNNING)
    {
        m_status      = Status::ScanFailed;
        m_active_wifi = nullptr;
        ESP_LOGW(TAG, "could not start async Wi-Fi scan: %d", scan_result);
        WiFi.scanDelete();
        return false;
    }

    m_active_wifi = nullptr;
    m_status      = Status::StationScanning;
    ESP_LOGI(TAG, "started async Wi-Fi scan");
    return true;
}

bool WifiManager::disconnect()
{
    const wifi_item_t* disconnected_wifi = m_connected_wifi ? m_connected_wifi : m_active_wifi;
    const bool station_disconnected = WiFi.disconnect(true, false);
    const bool ap_disconnected      = WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    if (m_reported_connected)
    {
        notifyDisconnected(disconnected_wifi);
    }

    m_active_wifi = nullptr;
    m_connected_wifi = nullptr;
    m_status      = Status::Idle;
    return station_disconnected || ap_disconnected;
}

void WifiManager::poll()
{
#ifdef BUILD_FTP_SERVER
    FtpServer::poll();
#endif

    if (m_status == Status::StationScanning)
    {
        const int network_count = WiFi.scanComplete();
        if (network_count == WIFI_SCAN_RUNNING)
        {
            return;
        }

        if (network_count < 0)
        {
            m_status      = Status::ScanFailed;
            m_active_wifi = nullptr;
            ESP_LOGW(TAG, "Wi-Fi scan failed: %d", network_count);
            WiFi.scanDelete();
            return;
        }

        const wifi_item_t* found_item = nullptr;
        for (size_t i = 0; i < m_station_count; ++i)
        {
            const wifi_item_t* item = station(i);
            if (item && scan_has_ssid(network_count, item->ssid))
            {
                ESP_LOGI(TAG, "found configured Wi-Fi network \"%s\"", item->ssid);
                found_item = item;
                break;
            }
        }

        WiFi.scanDelete();
        m_active_wifi = nullptr;
        if (found_item)
        {
            m_status = Status::Idle;
        }
        else
        {
            m_status = Status::NoKnownNetwork;
            ESP_LOGW(TAG, "no configured Wi-Fi stations were found in scan");
        }

        notifyScanFinished(found_item);
        return;
    }

    const Status current = status();

    if (is_connected_status(current))
    {
        m_status = current;
        if (!m_reported_connected && m_active_wifi)
        {
            notifyConnected(m_active_wifi);
        }
        return;
    }

    if (m_reported_connected)
    {
        notifyDisconnected(m_connected_wifi);
    }

    if (m_status == Status::StationConnecting)
    {
        const wl_status_t wifi_status = WiFi.status();
        if (wifi_status == WL_CONNECT_FAILED || wifi_status == WL_NO_SSID_AVAIL || wifi_status == WL_CONNECTION_LOST)
        {
            m_status = Status::ConnectFailed;
            ESP_LOGW(TAG, "Wi-Fi station connection failed, status=%d", static_cast<int>(wifi_status));
        }
    }
}

WifiManager::Status WifiManager::status() const
{
    if (m_status == Status::StationScanning)
    {
        return m_status;
    }

    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
    {
        return Status::AccessPoint;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        return Status::StationConnected;
    }

    return m_status;
}

const char* WifiManager::statusName() const
{
    return status_name(status());
}

void WifiManager::setOnConnectCallback(ConnectionCallback callback)
{
    m_on_connect = callback;
}

void WifiManager::setOnDisconnectCallback(ConnectionCallback callback)
{
    m_on_disconnect = callback;
}

void WifiManager::setOnScanFinished(ScanFinishedCallback callback)
{
    m_on_scan_finished = callback;
}

const wifi_item_t* WifiManager::activeWifi() const
{
    return m_active_wifi;
}

const wifi_item_t* WifiManager::connectedWifi() const
{
    return m_connected_wifi;
}

const char* WifiManager::generatedSoftApSsid() const
{
    return m_generated_soft_ap_ssid[0] != '\0' ? m_generated_soft_ap_ssid : nullptr;
}

const char* WifiManager::softApPassword() const
{
    if (status() != Status::AccessPoint || !m_active_wifi || !m_active_wifi->password || m_active_wifi->password[0] == '\0')
    {
        return nullptr;
    }

    return m_active_wifi->password;
}

void WifiManager::notifyConnected(const wifi_item_t* item)
{
    m_connected_wifi      = item;
    m_reported_connected = true;

    if (m_on_connect)
    {
        m_on_connect(item);
    }
}

void WifiManager::notifyDisconnected(const wifi_item_t* item)
{
    m_connected_wifi      = nullptr;
    m_reported_connected = false;

    if (m_on_disconnect)
    {
        m_on_disconnect(item);
    }
}

void WifiManager::notifyScanFinished(const wifi_item_t* item)
{
    if (m_on_scan_finished)
    {
        m_on_scan_finished(item);
    }
}

size_t WifiManager::cloudEndpointCount() const
{
    return m_cloud_endpoint_count;
}

cloud_item_t* WifiManager::cloudEndpoint(size_t index)
{
    return const_cast<cloud_item_t*>(static_cast<const WifiManager*>(this)->cloudEndpoint(index));
}

const cloud_item_t* WifiManager::cloudEndpoint(size_t index) const
{
    #ifndef BUILD_WITH_SECURITY
    const cloud_item_t* item = m_cloud_endpoint_head;
    while (item && index > 0)
    {
        item = static_cast<const cloud_item_t*>(item->next_node);
        --index;
    }
    return item;
    #else
    return index < m_cloud_endpoint_count ? &m_network_cfg.cloud[index] : nullptr;
    #endif
}

WifiManager::LoadResult WifiManager::lastLoadResult() const
{
    return m_last_load_result;
}

const char* WifiManager::lastLoadResultName() const
{
    return load_result_tostring(m_last_load_result);
}
