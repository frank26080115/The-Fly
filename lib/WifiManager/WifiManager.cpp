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
#include "Aegis.h"
#if defined(BUILD_FTP_SERVER) && BUILD_WITH_SECURITY_LEVEL <= 0
#include "FtpServer.h"
#endif
#include "IconLookup.h"
#include "MicroSdCard.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "utilfuncs.h"

bool wifiLowLevelInit(bool persistent);

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
constexpr uint32_t    kNetworkConfigMagic   = 0x54465749; // "TFWI"
constexpr uint32_t    kNetworkConfigVersion = 3;
constexpr const char* kNetworkNvsNamespace  = "wifi_cfg";
constexpr const char* kNetworkNvsBlobName   = "network";

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

uint8_t parse_icon_or_default(const char* icon, uint8_t default_icon)
{
    if (!icon || icon[0] == '\0')
    {
        return default_icon;
    }

    const uint8_t parsed = IconLookup::fromString(icon);
    return parsed == ICON_UNKNOWN ? default_icon : parsed;
}

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

bool valid_network_config_version(uint32_t version)
{
    return version == kNetworkConfigVersion;
}

void init_network_config_defaults(network_cfg_t& cfg)
{
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic   = kNetworkConfigMagic;
    cfg.version = kNetworkConfigVersion;
    cfg.security_level = BUILD_WITH_SECURITY_LEVEL;
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
    cfg.security_level = BUILD_WITH_SECURITY_LEVEL;
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
    cfg.access_point_count = cfg.access_point_count > kNetworkConfigAllowedEntriesAP ? static_cast<uint8_t>(kNetworkConfigAllowedEntriesAP) : cfg.access_point_count;
    cfg.cloud_endpoint_count = cfg.cloud_endpoint_count > kNetworkConfigCloudAllowedEntries ? static_cast<uint8_t>(kNetworkConfigCloudAllowedEntries) : cfg.cloud_endpoint_count;

    for (size_t i = 0; i < kNetworkConfigMaxEntries; ++i)
    {
        cfg.station[i].ssid[sizeof(cfg.station[i].ssid) - 1] = '\0';
        cfg.station[i].password[sizeof(cfg.station[i].password) - 1] = '\0';
        if (cfg.station[i].icon >= ICON_LAST)
        {
            cfg.station[i].icon = ICON_UNKNOWN;
        }
        cfg.cloud[i].password[sizeof(cfg.cloud[i].password) - 1] = '\0';
        cfg.cloud[i].url[sizeof(cfg.cloud[i].url) - 1] = '\0';
        if (cfg.cloud[i].icon >= ICON_LAST)
        {
            cfg.cloud[i].icon = ICON_UNKNOWN;
        }
    }

    for (size_t i = 0; i < kNetworkConfigMaxEntriesAP; ++i)
    {
        cfg.access_point[i].ssid[sizeof(cfg.access_point[i].ssid) - 1] = '\0';
        cfg.access_point[i].password[sizeof(cfg.access_point[i].password) - 1] = '\0';
        if (cfg.access_point[i].icon >= ICON_LAST)
        {
            cfg.access_point[i].icon = ICON_UNKNOWN;
        }
    }
}

bool cache_network_config_hash(const network_cfg_t& cfg)
{
    if (Aegis::cacheNetworkConfigHash(&cfg, sizeof(cfg)))
    {
        return true;
    }

    ESP_LOGW(TAG, "could not cache Wi-Fi config hash");
    return false;
}

bool parse_network_wifi_array(JsonDocument& doc,
                              const char* key,
                              wifi_item_t* items,
                              size_t item_capacity,
                              uint8_t& count,
                              uint8_t default_icon,
                              size_t& skipped)
{
    count = 0;
    JsonArray array = doc[key].as<JsonArray>();
    if (!array.isNull())
    {
        for (JsonVariant value : array)
        {
            if (count >= item_capacity)
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

            items[count].icon = parse_icon_or_default(icon, default_icon);
            ++count;
        }
    }

    for (size_t i = count; i < item_capacity; ++i)
    {
        memset(&items[i], 0, sizeof(items[i]));
    }

    return true;
}

bool parse_network_cloud_array(JsonDocument& doc, cloud_item_t* items, uint8_t& count, size_t& skipped)
{
    count = 0;
    JsonArray array = doc["cloud_uploads"].as<JsonArray>();
    if (!array.isNull())
    {
        for (JsonVariant value : array)
        {
            if (count >= kNetworkConfigCloudAllowedEntries)
            {
                ++skipped;
                continue;
            }

            JsonObject item_json = value.as<JsonObject>();
            const char* url      = item_json["url"].as<const char*>();
            const char* password = item_json["password"].as<const char*>();
            const char* icon     = item_json["icon"].as<const char*>();
            if (item_json.isNull() ||
                !copy_config_text(items[count].url, sizeof(items[count].url), url, true))
            {
                ++skipped;
                continue;
            }

            #if BUILD_WITH_SECURITY_LEVEL <= 0
            if (!copy_config_text(items[count].password, sizeof(items[count].password), password, true))
            {
                ++skipped;
                continue;
            }
            #else
            items[count].password[0] = '\0';
            #endif
            items[count].icon = parse_icon_or_default(icon, ICON_UNKNOWN);
            ++count;
        }
    }

    for (size_t i = count; i < kNetworkConfigCloudMaxEntries; ++i)
    {
        memset(&items[i], 0, sizeof(items[i]));
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

    parse_network_wifi_array(doc, "stations", cfg.station, kNetworkConfigMaxEntries, cfg.station_count, ICON_UNKNOWN, skipped);
    parse_network_wifi_array(doc, "access_points", cfg.access_point, kNetworkConfigAllowedEntriesAP, cfg.access_point_count, ICON_UNKNOWN, skipped);
    parse_network_cloud_array(doc, cfg.cloud, cfg.cloud_endpoint_count, skipped);
    sanitize_network_config(cfg);
    return skipped == 0;
}

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
    if (!ssid || ssid_size == 0)
    {
        return;
    }
    snprintf(ssid, ssid_size, "Fly-%08lu", static_cast<unsigned long>(generate_8_digit_nonce()));
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

void configure_soft_ap_security(wifi_config_t& config, const char* ssid, const char* password)
{
    memset(&config, 0, sizeof(config));

    strlcpy(reinterpret_cast<char*>(config.ap.ssid), ssid, sizeof(config.ap.ssid));
    strlcpy(reinterpret_cast<char*>(config.ap.password), password, sizeof(config.ap.password));

    config.ap.ssid_len       = strlen(ssid);
    config.ap.channel        = kSoftApChannel;
    config.ap.ssid_hidden    = kSoftApSsidHidden;
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
}

bool start_secure_soft_ap(const char* ssid, const char* password)
{
    wifi_config_t config = {};
    configure_soft_ap_security(config, ssid, password);

    if (!wifiLowLevelInit(false))
    {
        ESP_LOGW(TAG, "could not initialize Wi-Fi before SoftAP security configuration");
        return false;
    }

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED)
    {
        ESP_LOGW(TAG, "could not stop Wi-Fi before SoftAP security configuration: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "could not set Wi-Fi access point mode: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "could not configure WPA3-only SoftAP: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "could not start WPA3-only SoftAP: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

IPAddress current_soft_ap_ip()
{
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!netif)
    {
        return IPAddress();
    }

    esp_netif_ip_info_t ip_info = {};
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK)
    {
        return IPAddress();
    }

    return IPAddress(ip_info.ip.addr);
}

} // namespace

WifiManager::WifiManager()
{
    clear();
}

WifiManager::~WifiManager()
{
    clear();
}

#if BUILD_WITH_SECURITY_LEVEL <= 0
bool WifiManager::loadFromMicroSd(const char* path)
{
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
        ESP_LOGW(TAG, "Wi-Fi config import is too large: %llu bytes", static_cast<unsigned long long>(file_size));
        m_last_load_result = LoadResult::Ok;
        return true;
    }

    char* buffer = static_cast<char*>(malloc(static_cast<size_t>(file_size) + 1));
    if (!buffer)
    {
        file.close();
        ESP_LOGW(TAG, "could not allocate Wi-Fi config import buffer");
        m_last_load_result = LoadResult::Ok;
        return true;
    }

    const int bytes_read = file.read(buffer, static_cast<size_t>(file_size));
    file.close();

    if (bytes_read < 0 || static_cast<uint64_t>(bytes_read) != file_size)
    {
        free(buffer);
        ESP_LOGW(TAG, "could not read Wi-Fi config import");
        m_last_load_result = LoadResult::Ok;
        return true;
    }
    buffer[file_size] = '\0';

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, buffer);
    free(buffer);
    if (error)
    {
        ESP_LOGW(TAG, "could not parse Wi-Fi config import: %s", error.c_str());
        m_last_load_result = LoadResult::Ok;
        return true;
    }

    size_t skipped = 0;
    parse_network_config_json(doc, m_network_cfg, skipped);
    if (skipped > 0)
    {
        ESP_LOGW(TAG, "Wi-Fi config import skipped %u invalid or extra item(s)", static_cast<unsigned>(skipped));
    }

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
}
#endif

bool WifiManager::loadFromNvs()
{
    clear();

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNetworkNvsNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        m_last_load_result = LoadResult::Ok;
        ESP_LOGI(TAG, "no Wi-Fi config namespace in NVS; using defaults");
        return cache_network_config_hash(m_network_cfg);
    }
    if (err != ESP_OK)
    {
        m_last_load_result = LoadResult::FileOpenFailed;
        ESP_LOGW(TAG, "could not open Wi-Fi NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    size_t cfg_size = 0;
    err = nvs_get_blob(handle, kNetworkNvsBlobName, nullptr, &cfg_size);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_close(handle);
        m_last_load_result = LoadResult::Ok;
        ESP_LOGI(TAG, "no Wi-Fi config in NVS; using defaults");
        return cache_network_config_hash(m_network_cfg);
    }
    if (err != ESP_OK)
    {
        nvs_close(handle);
        m_last_load_result = LoadResult::FileReadFailed;
        ESP_LOGW(TAG, "could not query Wi-Fi config from NVS: %s", esp_err_to_name(err));
        return false;
    }
    if (cfg_size != sizeof(network_cfg_t))
    {
        nvs_close(handle);
        m_last_load_result = LoadResult::Ok;
        ESP_LOGW(TAG,
                 "ignoring incompatible Wi-Fi config size in NVS: stored=%u expected=%u",
                 static_cast<unsigned>(cfg_size),
                 static_cast<unsigned>(sizeof(network_cfg_t)));
        return cache_network_config_hash(m_network_cfg);
    }

    network_cfg_t& cfg = m_network_cfg;
    size_t read_size = sizeof(cfg);
    err = nvs_get_blob(handle, kNetworkNvsBlobName, &cfg, &read_size);
    nvs_close(handle);

    if (err != ESP_OK || read_size != sizeof(cfg))
    {
        m_last_load_result = LoadResult::FileReadFailed;
        ESP_LOGW(TAG, "could not load Wi-Fi config from NVS: %s size=%u", esp_err_to_name(err), static_cast<unsigned>(read_size));
        return false;
    }
    if (cfg.magic != kNetworkConfigMagic ||
        !valid_network_config_version(cfg.version) ||
        cfg.security_level != BUILD_WITH_SECURITY_LEVEL)
    {
        init_network_config_defaults(m_network_cfg);
        m_last_load_result = LoadResult::Ok;
        ESP_LOGW(TAG, "ignoring incompatible Wi-Fi config in NVS");
        return cache_network_config_hash(m_network_cfg);
    }

    sanitize_network_config(cfg);
    m_network_cfg = cfg;
    m_station_count = m_network_cfg.station_count;
    m_access_point_count = m_network_cfg.access_point_count;
    m_cloud_endpoint_count = m_network_cfg.cloud_endpoint_count;
    m_last_load_result = LoadResult::Ok;
    if (!cache_network_config_hash(m_network_cfg))
    {
        return false;
    }

    ESP_LOGI(TAG,
             "loaded Wi-Fi config from NVS: stations=%u access_points=%u cloud_endpoints=%u",
             static_cast<unsigned>(m_station_count),
             static_cast<unsigned>(m_access_point_count),
             static_cast<unsigned>(m_cloud_endpoint_count));
    return true;
}

bool WifiManager::saveToNvs()
{
    m_network_cfg.station_count = static_cast<uint8_t>(m_station_count);
    m_network_cfg.access_point_count = static_cast<uint8_t>(m_access_point_count);
    m_network_cfg.cloud_endpoint_count = static_cast<uint8_t>(m_cloud_endpoint_count);
    sanitize_network_config(m_network_cfg);
    m_station_count = m_network_cfg.station_count;
    m_access_point_count = m_network_cfg.access_point_count;
    m_cloud_endpoint_count = m_network_cfg.cloud_endpoint_count;

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
    return cache_network_config_hash(m_network_cfg);
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

void WifiManager::clear()
{
    init_network_config_defaults(m_network_cfg);
    m_station_count = 0;
    m_access_point_count = 0;
    m_cloud_endpoint_count = 0;
    m_active_wifi = nullptr;
    m_connected_wifi = nullptr;
    m_reported_connected = false;
    m_last_load_result = LoadResult::Ok;
    cache_network_config_hash(m_network_cfg);
}

const char* WifiManager::timezone() const
{
    return m_network_cfg.timezone[0] != '\0' ? m_network_cfg.timezone : kDefaultTimezone;
}

const char* WifiManager::ntpServer(size_t index) const
{
    if (index >= kNtpServerCount)
    {
        return nullptr;
    }
    return m_network_cfg.ntp_server[index][0] != '\0' ? m_network_cfg.ntp_server[index] : kDefaultNtpServers[index];
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
    return index < m_station_count ? &m_network_cfg.station[index] : nullptr;
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
    return index < m_access_point_count ? &m_network_cfg.access_point[index] : nullptr;
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
    resetSoftApClientTracking();

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
        resetSoftApClientTracking();
        m_status      = Status::AccessPointFailed;
        m_active_wifi = nullptr;
        ESP_LOGW(TAG, "cannot start missing Wi-Fi access point");
        return false;
    }

    shutdown_for_wifi_activation();
    resetSoftApClientTracking();

    if (m_reported_connected)
    {
        notifyDisconnected(m_connected_wifi);
    }

    WiFi.disconnect(true, false);

    const char* password = access_point->password && access_point->password[0] != '\0' ? access_point->password : nullptr;
    if (!password || strlen(password) < 8)
    {
        resetSoftApClientTracking();
        m_status      = Status::AccessPointFailed;
        m_active_wifi = nullptr;
        ESP_LOGW(TAG, "cannot start WPA3-only Wi-Fi access point \"%s\" without an 8+ character password", access_point->ssid);
        return false;
    }

    if (!start_secure_soft_ap(access_point->ssid, password))
    {
        WiFi.softAPdisconnect(true);
        resetSoftApClientTracking();
        m_status      = Status::AccessPointFailed;
        m_active_wifi = nullptr;
        ESP_LOGW(TAG, "could not start WPA3-only Wi-Fi access point \"%s\"", access_point->ssid);
        return false;
    }

    m_active_wifi = access_point;
    m_status      = Status::AccessPoint;
    resetWebCounters();
    notifyConnected(access_point);
    ESP_LOGI(TAG, "started Wi-Fi access point \"%s\" at %s", access_point->ssid, current_soft_ap_ip().toString().c_str());
    return true;
}

bool WifiManager::startGeneratedSoftAp()
{
    format_generated_soft_ap_ssid(m_generated_soft_ap_ssid, sizeof(m_generated_soft_ap_ssid));
    format_generated_soft_ap_password(m_generated_soft_ap_password, sizeof(m_generated_soft_ap_password));

    strlcpy(m_generated_soft_ap.ssid, m_generated_soft_ap_ssid, sizeof(m_generated_soft_ap.ssid));
    strlcpy(m_generated_soft_ap.password, m_generated_soft_ap_password, sizeof(m_generated_soft_ap.password));

    m_generated_soft_ap.icon = ICON_UNKNOWN;
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
    resetSoftApClientTracking();

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
    resetSoftApClientTracking();
    return station_disconnected || ap_disconnected;
}

void WifiManager::poll()
{
#if defined(BUILD_FTP_SERVER) && BUILD_WITH_SECURITY_LEVEL <= 0
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
        if (current == Status::AccessPoint)
        {
            updateSoftApClientTracking();
        }
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

bool WifiManager::isGeneratedSoftApActive() const
{
    return status() == Status::AccessPoint && m_active_wifi == &m_generated_soft_ap;
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

bool WifiManager::softApClientMac(uint8_t out[6]) const
{
    if (!out || !m_soft_ap_client_connected)
    {
        return false;
    }

    memcpy(out, m_soft_ap_client_mac, sizeof(m_soft_ap_client_mac));
    return true;
}

uint32_t WifiManager::softApClientConnectionCount() const
{
    return m_soft_ap_client_connection_count;
}

void WifiManager::noteWebPageLoad()
{
    ++m_web_page_load_count;
}

void WifiManager::noteWebLogin()
{
    ++m_web_login_count;
}

void WifiManager::noteWebSave()
{
    ++m_web_save_count;
}

void WifiManager::noteWebError()
{
    ++m_web_error_count;
}

void WifiManager::noteWebDownload()
{
    ++m_web_download_count;
}

uint32_t WifiManager::webPageLoadCount() const
{
    return m_web_page_load_count;
}

uint32_t WifiManager::webLoginCount() const
{
    return m_web_login_count;
}

uint32_t WifiManager::webSaveCount() const
{
    return m_web_save_count;
}

uint32_t WifiManager::webErrorCount() const
{
    return m_web_error_count;
}

uint32_t WifiManager::webDownloadCount() const
{
    return m_web_download_count;
}

void WifiManager::resetWebCounters()
{
    m_web_page_load_count = 0;
    m_web_login_count = 0;
    m_web_save_count = 0;
    m_web_error_count = 0;
    m_web_download_count = 0;
}

void WifiManager::resetSoftApClientTracking()
{
    m_soft_ap_client_connected = false;
    memset(m_soft_ap_client_mac, 0, sizeof(m_soft_ap_client_mac));
    m_soft_ap_client_connection_count = 0;
}

void WifiManager::updateSoftApClientTracking()
{
    if (status() != Status::AccessPoint)
    {
        m_soft_ap_client_connected = false;
        memset(m_soft_ap_client_mac, 0, sizeof(m_soft_ap_client_mac));
        return;
    }

    wifi_sta_list_t stations = {};
    const esp_err_t err = esp_wifi_ap_get_sta_list(&stations);
    if (err != ESP_OK || stations.num <= 0)
    {
        m_soft_ap_client_connected = false;
        memset(m_soft_ap_client_mac, 0, sizeof(m_soft_ap_client_mac));
        return;
    }

    const uint8_t* mac = stations.sta[0].mac;
    const bool same_client = m_soft_ap_client_connected && memcmp(m_soft_ap_client_mac, mac, sizeof(m_soft_ap_client_mac)) == 0;
    if (!same_client)
    {
        ++m_soft_ap_client_connection_count;
        memcpy(m_soft_ap_client_mac, mac, sizeof(m_soft_ap_client_mac));
    }
    m_soft_ap_client_connected = true;
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
    return index < m_cloud_endpoint_count ? &m_network_cfg.cloud[index] : nullptr;
}

WifiManager::LoadResult WifiManager::lastLoadResult() const
{
    return m_last_load_result;
}

const char* WifiManager::lastLoadResultName() const
{
    return load_result_tostring(m_last_load_result);
}
