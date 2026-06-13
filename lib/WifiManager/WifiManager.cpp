// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "WifiManager.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "AudioFileRecorder.h"
#include "AudioManager.h"
#include "AsyncFsManager.h"
#include "BluetoothManager.h"
#include "Aegis.h"
#include "HapticsWrapper.h"
#if defined(BUILD_FTP_SERVER) && BUILD_WITH_SECURITY_LEVEL <= 1
#include "FtpServer.h"
#endif
#include "IconLookup.h"
#include "MicroSdCard.h"
#include "esp_err.h"
#include "dbg_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "utilfuncs.h"

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

bool wifiLowLevelInit(bool persistent);

namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr const char* TAG              = "WifiManager";
constexpr size_t      kMaxJsonFileSize = 16 * 1024;

constexpr const char* kDefaultTimezone     = "UTC0";
constexpr const char* kDefaultNtpServers[] = {
    "pool.ntp.org",
    "time.nist.gov",
    "time.google.com",
};
constexpr int         kSoftApSsidHidden     = 0;
constexpr int         kSoftApMaxConnection  = 1;
constexpr uint8_t     kSoftApChannels[]     = {1, 6, 11};
constexpr size_t      kSoftApChannelCount   = sizeof(kSoftApChannels) / sizeof(kSoftApChannels[0]);
constexpr uint8_t     kSoftApFallbackChannel = kSoftApChannels[0];
constexpr uint32_t    kNetworkConfigMagic   = 0x54465749; // "TFWI"
constexpr uint32_t    kNetworkConfigVersion = 3;
constexpr const char* kNetworkNvsNamespace  = "wifi_cfg";
constexpr const char* kNetworkNvsBlobName   = "network";

static_assert(kSoftApChannelCount > 0, "SoftAP channel list must not be empty");

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

network_cfg_t                     g_network_cfg          = {};
size_t                            g_station_count        = 0;
size_t                            g_access_point_count   = 0;
size_t                            g_cloud_endpoint_count = 0;
WifiManager::LoadResult           g_last_load_result     = WifiManager::LoadResult::Ok;
WifiManager::Status               g_status               = WifiManager::Status::Idle;
wifi_item_t                       g_active_wifi          = {};
wifi_item_t                       g_connected_wifi       = {};
bool                              g_has_active_wifi      = false;
bool                              g_has_connected_wifi   = false;
bool                              g_active_generated_ap  = false;
bool                              g_wifi_has_started     = false;
bool                              g_reported_connected   = false;
WifiManager::ConnectionCallback   g_on_connect           = nullptr;
WifiManager::ConnectionCallback   g_on_disconnect        = nullptr;
WifiManager::ScanFinishedCallback g_on_scan_finished     = nullptr;
char                              g_generated_soft_ap_ssid[WifiManager::kGeneratedSoftApSsidLength + 1]         = {};
char                              g_generated_soft_ap_password[WifiManager::kGeneratedSoftApPasswordLength + 1] = {};
wifi_item_t                       g_generated_soft_ap                                                           = {};
uint32_t                          g_web_page_load_count                                                         = 0;
uint32_t                          g_web_login_count                                                             = 0;
uint32_t                          g_web_save_count                                                              = 0;
uint32_t                          g_web_error_count                                                             = 0;
uint32_t                          g_web_download_count                                                          = 0;
bool                              g_soft_ap_client_connected                                                    = false;
uint8_t                           g_soft_ap_client_mac[6]                                                       = {};
uint32_t                          g_soft_ap_client_connection_count                                             = 0;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

const char* load_result_tostring(WifiManager::LoadResult result);
const char* status_name(WifiManager::Status status);
uint8_t     parse_icon_or_default(const char* icon, uint8_t default_icon);
bool        copy_config_text(char* dst, size_t dst_size, const char* value, bool required);
bool        valid_network_config_version(uint32_t version);
void        init_network_config_defaults(network_cfg_t& cfg);
void        sanitize_network_config(network_cfg_t& cfg);
void        apply_timezone(const char* timezone);
bool        cache_network_config_hash(const network_cfg_t& cfg);
bool        parse_network_wifi_array(
    JsonDocument& doc, const char* array_name, wifi_item_t* items, uint8_t& count, size_t& skipped);
bool      parse_network_cloud_array(JsonDocument& doc, cloud_item_t* items, uint8_t& count, size_t& skipped);
bool      parse_network_config_json(JsonDocument& doc, network_cfg_t& cfg, size_t& skipped);
bool      scan_has_ssid(int network_count, const char* ssid);
bool      is_connected_status(WifiManager::Status status);
void      shutdown_for_wifi_activation();
void      format_generated_soft_ap_ssid(char* ssid, size_t ssid_size);
void      format_generated_soft_ap_password(char* password, size_t password_size);
void      configure_soft_ap_security(wifi_config_t& config, const char* ssid, const char* password, uint8_t channel);
bool      start_secure_soft_ap(const char* ssid, const char* password);
IPAddress current_soft_ap_ip();
const wifi_item_t* active_wifi_ptr();
const wifi_item_t* connected_wifi_ptr();
void               set_active_wifi(const wifi_item_t* item, bool generated_soft_ap);
void               clear_active_wifi();
void               set_connected_wifi(const wifi_item_t* item);
void               clear_connected_wifi();
uint8_t            get_random_wifi_channel();

} // namespace

namespace WifiManager
{
bool connectToHotspot(const wifi_item_t* hotspot, bool shutdown_first);
void resetWebCounters();
void resetSoftApClientTracking();
void updateSoftApClientTracking();
void notifyConnected(const wifi_item_t* item);
void notifyDisconnected(const wifi_item_t* item);
void notifyScanFinished(const wifi_item_t* item);
} // namespace WifiManager

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

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
        DBG_LOGI(TAG, "microSD is not ready; keeping Wi-Fi config from NVS");
        return true;
    }

    FsFile file;
    if (!file.open(import_path, O_RDONLY))
    {
        DBG_LOGI(TAG, "no Wi-Fi JSON import found at %s; keeping Wi-Fi config from NVS", import_path);
        return true;
    }

    const uint64_t file_size = file.fileSize();
    if (file_size > kMaxJsonFileSize)
    {
        file.close();
        DBG_LOGW(TAG, "Wi-Fi config import is too large: %llu bytes", static_cast<unsigned long long>(file_size));
        g_last_load_result = LoadResult::Ok;
        return true;
    }

    char* buffer = static_cast<char*>(malloc(static_cast<size_t>(file_size) + 1));
    if (!buffer)
    {
        file.close();
        DBG_LOGW(TAG, "could not allocate Wi-Fi config import buffer");
        g_last_load_result = LoadResult::Ok;
        return true;
    }

    const int bytes_read = file.read(buffer, static_cast<size_t>(file_size));
    file.close();

    if (bytes_read < 0 || static_cast<uint64_t>(bytes_read) != file_size)
    {
        free(buffer);
        DBG_LOGW(TAG, "could not read Wi-Fi config import");
        g_last_load_result = LoadResult::Ok;
        return true;
    }
    buffer[file_size] = '\0';

    JsonDocument               doc;
    const DeserializationError error = deserializeJson(doc, buffer);
    free(buffer);
    if (error)
    {
        DBG_LOGW(TAG, "could not parse Wi-Fi config import: %s", error.c_str());
        g_last_load_result = LoadResult::Ok;
        return true;
    }

    size_t skipped = 0;
    parse_network_config_json(doc, g_network_cfg, skipped);
    if (skipped > 0)
    {
        DBG_LOGW(TAG, "Wi-Fi config import skipped %u invalid or extra item(s)", static_cast<unsigned>(skipped));
    }

    g_station_count      = g_network_cfg.station_count;
    g_access_point_count = g_network_cfg.access_point_count;
#ifdef BUILD_CLOUD_FEATURES
    g_cloud_endpoint_count = g_network_cfg.cloud_endpoint_count;
#endif

    if (!saveToNvs())
    {
        return false;
    }

    g_last_load_result = LoadResult::Ok;
    DBG_LOGI(TAG,
             "imported Wi-Fi config into NVS: stations=%u access_points=%u cloud_endpoints=%u",
             static_cast<unsigned>(g_station_count),
             static_cast<unsigned>(g_access_point_count),
             static_cast<unsigned>(g_cloud_endpoint_count));
    return true;
}
#endif

bool WifiManager::loadFromNvs()
{
    clear();

    nvs_handle_t handle = 0;
    esp_err_t    err    = nvs_open(kNetworkNvsNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        g_last_load_result = LoadResult::Ok;
        DBG_LOGI(TAG, "no Wi-Fi config namespace in NVS; using defaults");
        apply_timezone(timezone());
        return cache_network_config_hash(g_network_cfg);
    }
    if (err != ESP_OK)
    {
        g_last_load_result = LoadResult::FileOpenFailed;
        DBG_LOGW(TAG, "could not open Wi-Fi NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    size_t cfg_size = 0;
    err             = nvs_get_blob(handle, kNetworkNvsBlobName, nullptr, &cfg_size);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_close(handle);
        g_last_load_result = LoadResult::Ok;
        DBG_LOGI(TAG, "no Wi-Fi config in NVS; using defaults");
        apply_timezone(timezone());
        return cache_network_config_hash(g_network_cfg);
    }
    if (err != ESP_OK)
    {
        nvs_close(handle);
        g_last_load_result = LoadResult::FileReadFailed;
        DBG_LOGW(TAG, "could not query Wi-Fi config from NVS: %s", esp_err_to_name(err));
        return false;
    }
    if (cfg_size != sizeof(network_cfg_t))
    {
        nvs_close(handle);
        g_last_load_result = LoadResult::Ok;
        DBG_LOGW(TAG,
                 "ignoring incompatible Wi-Fi config size in NVS: stored=%u expected=%u",
                 static_cast<unsigned>(cfg_size),
                 static_cast<unsigned>(sizeof(network_cfg_t)));
        return cache_network_config_hash(g_network_cfg);
    }

    network_cfg_t& cfg       = g_network_cfg;
    size_t         read_size = sizeof(cfg);
    err                      = nvs_get_blob(handle, kNetworkNvsBlobName, &cfg, &read_size);
    nvs_close(handle);

    if (err != ESP_OK || read_size != sizeof(cfg))
    {
        g_last_load_result = LoadResult::FileReadFailed;
        DBG_LOGW(TAG,
                 "could not load Wi-Fi config from NVS: %s size=%u",
                 esp_err_to_name(err),
                 static_cast<unsigned>(read_size));
        return false;
    }
    if (cfg.magic != kNetworkConfigMagic || !valid_network_config_version(cfg.version) ||
        cfg.security_level != BUILD_WITH_SECURITY_LEVEL)
    {
        init_network_config_defaults(g_network_cfg);
        g_last_load_result = LoadResult::Ok;
        DBG_LOGW(TAG, "ignoring incompatible Wi-Fi config in NVS");
        apply_timezone(timezone());
        return cache_network_config_hash(g_network_cfg);
    }

    sanitize_network_config(cfg);
    g_network_cfg        = cfg;
    g_station_count      = g_network_cfg.station_count;
    g_access_point_count = g_network_cfg.access_point_count;
#ifdef BUILD_CLOUD_FEATURES
    g_cloud_endpoint_count = g_network_cfg.cloud_endpoint_count;
#endif
    g_last_load_result = LoadResult::Ok;
    apply_timezone(timezone());
    if (!cache_network_config_hash(g_network_cfg))
    {
        return false;
    }

    DBG_LOGI(TAG,
             "loaded Wi-Fi config from NVS: stations=%u access_points=%u cloud_endpoints=%u",
             static_cast<unsigned>(g_station_count),
             static_cast<unsigned>(g_access_point_count),
             static_cast<unsigned>(g_cloud_endpoint_count));
    return true;
}

bool WifiManager::saveToNvs()
{
    g_network_cfg.station_count        = static_cast<uint8_t>(g_station_count);
    g_network_cfg.access_point_count   = static_cast<uint8_t>(g_access_point_count);
    g_network_cfg.cloud_endpoint_count = static_cast<uint8_t>(g_cloud_endpoint_count);
    sanitize_network_config(g_network_cfg);
    g_station_count      = g_network_cfg.station_count;
    g_access_point_count = g_network_cfg.access_point_count;
#ifdef BUILD_CLOUD_FEATURES
    g_cloud_endpoint_count = g_network_cfg.cloud_endpoint_count;
#endif
    apply_timezone(timezone());

    nvs_handle_t handle = 0;
    esp_err_t    err    = nvs_open(kNetworkNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        g_last_load_result = LoadResult::FileOpenFailed;
        DBG_LOGW(TAG, "could not open Wi-Fi NVS namespace for write: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(handle, kNetworkNvsBlobName, &g_network_cfg, sizeof(g_network_cfg));
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK)
    {
        g_last_load_result = LoadResult::FileWriteFailed;
        DBG_LOGW(TAG, "could not save Wi-Fi config to NVS: %s", esp_err_to_name(err));
        return false;
    }

    g_last_load_result = LoadResult::Ok;
    return cache_network_config_hash(g_network_cfg);
}

bool WifiManager::copyConfig(network_cfg_t& out)
{
    out = g_network_cfg;
    sanitize_network_config(out);
    return true;
}

bool WifiManager::replaceConfig(const network_cfg_t& config)
{
    network_cfg_t staged = config;
    sanitize_network_config(staged);

    g_network_cfg        = staged;
    g_station_count      = g_network_cfg.station_count;
    g_access_point_count = g_network_cfg.access_point_count;
#ifdef BUILD_CLOUD_FEATURES
    g_cloud_endpoint_count = g_network_cfg.cloud_endpoint_count;
#else
    g_cloud_endpoint_count = 0;
#endif

    return saveToNvs();
}

void WifiManager::clear()
{
    init_network_config_defaults(g_network_cfg);
    g_station_count        = 0;
    g_access_point_count   = 0;
    g_cloud_endpoint_count = 0;
    clear_active_wifi();
    clear_connected_wifi();
    g_reported_connected   = false;
    g_last_load_result     = LoadResult::Ok;
    cache_network_config_hash(g_network_cfg);
    apply_timezone(timezone());
}

const char* WifiManager::timezone()
{
    return g_network_cfg.timezone[0] != '\0' ? g_network_cfg.timezone : kDefaultTimezone;
}

const char* WifiManager::ntpServer(size_t index)
{
    if (index >= kNtpServerCount)
    {
        return nullptr;
    }
    return g_network_cfg.ntp_server[index][0] != '\0' ? g_network_cfg.ntp_server[index] : kDefaultNtpServers[index];
}

size_t WifiManager::stationCount()
{
    return g_station_count;
}

const wifi_item_t* WifiManager::station(size_t index)
{
    return index < g_station_count ? &g_network_cfg.station[index] : nullptr;
}

size_t WifiManager::accessPointCount()
{
    return g_access_point_count;
}

const wifi_item_t* WifiManager::accessPoint(size_t index)
{
    return index < g_access_point_count ? &g_network_cfg.access_point[index] : nullptr;
}

bool WifiManager::connectToHotspot(const wifi_item_t* hotspot)
{
    return connectToHotspot(hotspot, true);
}

bool WifiManager::connectToHotspot(const wifi_item_t* hotspot, bool shutdown_first)
{
    if (!hotspot || !hotspot->ssid || hotspot->ssid[0] == '\0')
    {
        g_status      = Status::ConnectFailed;
        clear_active_wifi();
        DBG_LOGW(TAG, "cannot connect to missing Wi-Fi hotspot");
        return false;
    }

    if (shutdown_first)
    {
        shutdown_for_wifi_activation();
    }
    resetSoftApClientTracking();

    if (g_reported_connected)
    {
        notifyDisconnected(connected_wifi_ptr());
    }

    WiFi.softAPdisconnect(true);
    if (!WiFi.mode(WIFI_STA))
    {
        g_status      = Status::ConnectFailed;
        clear_active_wifi();
        DBG_LOGW(TAG, "could not switch Wi-Fi to station mode");
        return false;
    }
    g_wifi_has_started = true;

    const char* password = hotspot->password && hotspot->password[0] != '\0' ? hotspot->password : nullptr;
    WiFi.begin(hotspot->ssid, password);

    set_active_wifi(hotspot, false);
    g_status      = Status::StationConnecting;
    DBG_LOGI(TAG, "started Wi-Fi station connection to \"%s\"", hotspot->ssid);
    return true;
}

bool WifiManager::startSoftAp(const wifi_item_t* access_point)
{
    if (!access_point || !access_point->ssid || access_point->ssid[0] == '\0')
    {
        resetSoftApClientTracking();
        g_status      = Status::AccessPointFailed;
        clear_active_wifi();
        DBG_LOGW(TAG, "cannot start missing Wi-Fi access point");
        return false;
    }

    shutdown_for_wifi_activation();
    resetSoftApClientTracking();

    if (g_reported_connected)
    {
        notifyDisconnected(connected_wifi_ptr());
    }

    WiFi.disconnect(true, false);

    const char* password =
        access_point->password && access_point->password[0] != '\0' ? access_point->password : nullptr;
    if (!password || strlen(password) < 8)
    {
        resetSoftApClientTracking();
        g_status      = Status::AccessPointFailed;
        clear_active_wifi();
        DBG_LOGW(TAG,
                 "cannot start WPA3-only Wi-Fi access point \"%s\" without an 8+ character password",
                 access_point->ssid);
        return false;
    }

    if (!start_secure_soft_ap(access_point->ssid, password))
    {
        WiFi.softAPdisconnect(true);
        resetSoftApClientTracking();
        g_status      = Status::AccessPointFailed;
        clear_active_wifi();
        DBG_LOGW(TAG, "could not start WPA3-only Wi-Fi access point \"%s\"", access_point->ssid);
        return false;
    }

    set_active_wifi(access_point, access_point == &g_generated_soft_ap);
    g_status           = Status::AccessPoint;
    g_wifi_has_started = true;
    resetWebCounters();
    notifyConnected(active_wifi_ptr());
    DBG_LOGI(TAG,
             "started Wi-Fi access point \"%s\" at %s",
             access_point->ssid,
             current_soft_ap_ip().toString().c_str());
    return true;
}

bool WifiManager::startGeneratedSoftAp()
{
    format_generated_soft_ap_ssid(g_generated_soft_ap_ssid, sizeof(g_generated_soft_ap_ssid));
    format_generated_soft_ap_password(g_generated_soft_ap_password, sizeof(g_generated_soft_ap_password));

    strlcpy(g_generated_soft_ap.ssid, g_generated_soft_ap_ssid, sizeof(g_generated_soft_ap.ssid));
    strlcpy(g_generated_soft_ap.password, g_generated_soft_ap_password, sizeof(g_generated_soft_ap.password));

    g_generated_soft_ap.icon = ICON_UNKNOWN;
    return startSoftAp(&g_generated_soft_ap);
}

bool WifiManager::scanAndConnect()
{
    if (g_status == Status::StationScanning)
    {
        return true;
    }

    if (g_reported_connected)
    {
        notifyDisconnected(connected_wifi_ptr());
    }
    resetSoftApClientTracking();

    if (g_station_count == 0)
    {
        g_status      = Status::NoKnownNetwork;
        clear_active_wifi();
        DBG_LOGW(TAG, "cannot scan/connect: no configured Wi-Fi stations");
        return false;
    }

    shutdown_for_wifi_activation();

    WiFi.softAPdisconnect(true);
    if (!WiFi.mode(WIFI_STA))
    {
        g_status      = Status::ScanFailed;
        clear_active_wifi();
        DBG_LOGW(TAG, "could not switch Wi-Fi to station mode for scan");
        return false;
    }
    g_wifi_has_started = true;

    const int scan_result = WiFi.scanNetworks(true);
    if (scan_result != WIFI_SCAN_RUNNING)
    {
        g_status      = Status::ScanFailed;
        clear_active_wifi();
        DBG_LOGW(TAG, "could not start async Wi-Fi scan: %d", scan_result);
        WiFi.scanDelete();
        return false;
    }

    clear_active_wifi();
    g_status      = Status::StationScanning;
    DBG_LOGI(TAG, "started async Wi-Fi scan");
    return true;
}

bool WifiManager::disconnect()
{
    const wifi_item_t* disconnected_wifi =
        connected_wifi_ptr() ? connected_wifi_ptr() : active_wifi_ptr();
    const bool         station_disconnected = WiFi.disconnect(true, false);
    const bool         ap_disconnected      = WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    if (g_reported_connected)
    {
        notifyDisconnected(disconnected_wifi);
    }

    clear_active_wifi();
    clear_connected_wifi();
    g_status         = Status::Idle;
    resetSoftApClientTracking();
    return station_disconnected || ap_disconnected;
}

bool WifiManager::wifiHasStarted()
{
    return g_wifi_has_started;
}

void WifiManager::poll()
{
#if defined(BUILD_FTP_SERVER) && BUILD_WITH_SECURITY_LEVEL <= 1
    FtpServer::poll();
#endif

    if (g_status == Status::StationScanning)
    {
        const int network_count = WiFi.scanComplete();
        if (network_count == WIFI_SCAN_RUNNING)
        {
            return;
        }

        if (network_count < 0)
        {
            g_status      = Status::ScanFailed;
            clear_active_wifi();
            DBG_LOGW(TAG, "Wi-Fi scan failed: %d", network_count);
            WiFi.scanDelete();
            return;
        }

        const wifi_item_t* found_item = nullptr;
        for (size_t i = 0; i < g_station_count; ++i)
        {
            const wifi_item_t* item = station(i);
            if (item && scan_has_ssid(network_count, item->ssid))
            {
                DBG_LOGI(TAG, "found configured Wi-Fi network \"%s\"", item->ssid);
                found_item = item;
                break;
            }
        }

        WiFi.scanDelete();
        clear_active_wifi();
        if (found_item)
        {
            g_status = Status::Idle;
        }
        else
        {
            g_status = Status::NoKnownNetwork;
            DBG_LOGW(TAG, "no configured Wi-Fi stations were found in scan");
        }

        notifyScanFinished(found_item);
        return;
    }

    const Status current = status();

    if (is_connected_status(current))
    {
        g_status = current;
        if (current == Status::AccessPoint)
        {
            updateSoftApClientTracking();
        }
        if (!g_reported_connected && active_wifi_ptr())
        {
            notifyConnected(active_wifi_ptr());
        }
        return;
    }

    if (g_reported_connected)
    {
        notifyDisconnected(connected_wifi_ptr());
    }

    if (g_status == Status::StationConnecting)
    {
        const wl_status_t wifi_status = WiFi.status();
        if (wifi_status == WL_CONNECT_FAILED || wifi_status == WL_NO_SSID_AVAIL || wifi_status == WL_CONNECTION_LOST)
        {
            g_status = Status::ConnectFailed;
            DBG_LOGW(TAG, "Wi-Fi station connection failed, status=%d", static_cast<int>(wifi_status));
        }
    }
}

WifiManager::Status WifiManager::status()
{
    if (g_status == Status::StationScanning)
    {
        return g_status;
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

    return g_status;
}

const char* WifiManager::statusName()
{
    return status_name(status());
}

void WifiManager::setOnConnectCallback(ConnectionCallback callback)
{
    g_on_connect = callback;
}

void WifiManager::setOnDisconnectCallback(ConnectionCallback callback)
{
    g_on_disconnect = callback;
}

void WifiManager::setOnScanFinished(ScanFinishedCallback callback)
{
    g_on_scan_finished = callback;
}

const wifi_item_t* WifiManager::activeWifi()
{
    return active_wifi_ptr();
}

const wifi_item_t* WifiManager::connectedWifi()
{
    return connected_wifi_ptr();
}

bool WifiManager::isGeneratedSoftApActive()
{
    return status() == Status::AccessPoint && g_has_active_wifi && g_active_generated_ap;
}

const char* WifiManager::generatedSoftApSsid()
{
    return g_generated_soft_ap_ssid[0] != '\0' ? g_generated_soft_ap_ssid : nullptr;
}

const char* WifiManager::softApPassword()
{
    const wifi_item_t* active = active_wifi_ptr();
    if (status() != Status::AccessPoint || !active || active->password[0] == '\0')
    {
        return nullptr;
    }

    return active->password;
}

IPAddress WifiManager::softApIp()
{
    if (status() != Status::AccessPoint)
    {
        return IPAddress();
    }

    return current_soft_ap_ip();
}

bool WifiManager::softApClientMac(uint8_t out[6])
{
    if (!out || !g_soft_ap_client_connected)
    {
        return false;
    }

    memcpy(out, g_soft_ap_client_mac, sizeof(g_soft_ap_client_mac));
    return true;
}

uint32_t WifiManager::softApClientConnectionCount()
{
    return g_soft_ap_client_connection_count;
}

void WifiManager::noteWebPageLoad()
{
    ++g_web_page_load_count;
}

void WifiManager::noteWebLogin()
{
    ++g_web_login_count;
}

void WifiManager::noteWebSave()
{
    ++g_web_save_count;
}

void WifiManager::noteWebError()
{
    ++g_web_error_count;
}

void WifiManager::noteWebDownload()
{
    ++g_web_download_count;
}

uint32_t WifiManager::webPageLoadCount()
{
    return g_web_page_load_count;
}

uint32_t WifiManager::webLoginCount()
{
    return g_web_login_count;
}

uint32_t WifiManager::webSaveCount()
{
    return g_web_save_count;
}

uint32_t WifiManager::webErrorCount()
{
    return g_web_error_count;
}

uint32_t WifiManager::webDownloadCount()
{
    return g_web_download_count;
}

void WifiManager::resetWebCounters()
{
    g_web_page_load_count = 0;
    g_web_login_count     = 0;
    g_web_save_count      = 0;
    g_web_error_count     = 0;
    g_web_download_count  = 0;
}

void WifiManager::resetSoftApClientTracking()
{
    g_soft_ap_client_connected = false;
    memset(g_soft_ap_client_mac, 0, sizeof(g_soft_ap_client_mac));
    g_soft_ap_client_connection_count = 0;
}

void WifiManager::updateSoftApClientTracking()
{
    if (status() != Status::AccessPoint)
    {
        g_soft_ap_client_connected = false;
        memset(g_soft_ap_client_mac, 0, sizeof(g_soft_ap_client_mac));
        return;
    }

    wifi_sta_list_t stations = {};
    const esp_err_t err      = esp_wifi_ap_get_sta_list(&stations);
    if (err != ESP_OK || stations.num <= 0)
    {
        g_soft_ap_client_connected = false;
        memset(g_soft_ap_client_mac, 0, sizeof(g_soft_ap_client_mac));
        return;
    }

    const uint8_t* mac = stations.sta[0].mac;
    const bool     same_client =
        g_soft_ap_client_connected && memcmp(g_soft_ap_client_mac, mac, sizeof(g_soft_ap_client_mac)) == 0;
    if (!same_client)
    {
        ++g_soft_ap_client_connection_count;
        memcpy(g_soft_ap_client_mac, mac, sizeof(g_soft_ap_client_mac));
        haptic_play_done();
    }
    g_soft_ap_client_connected = true;
}

void WifiManager::notifyConnected(const wifi_item_t* item)
{
    set_connected_wifi(item);
    g_reported_connected = g_has_connected_wifi;

    if (g_on_connect)
    {
        g_on_connect(connected_wifi_ptr());
    }
}

void WifiManager::notifyDisconnected(const wifi_item_t* item)
{
    wifi_item_t disconnected = {};
    const bool  has_item     = item != nullptr;
    if (has_item)
    {
        disconnected = *item;
    }

    clear_connected_wifi();
    g_reported_connected = false;

    if (g_on_disconnect)
    {
        g_on_disconnect(has_item ? &disconnected : nullptr);
    }
}

void WifiManager::notifyScanFinished(const wifi_item_t* item)
{
    if (g_on_scan_finished)
    {
        g_on_scan_finished(item);
    }
}

size_t WifiManager::cloudEndpointCount()
{
#ifdef BUILD_CLOUD_FEATURES
    return g_cloud_endpoint_count;
#else
    return 0;
#endif
}

const cloud_item_t* WifiManager::cloudEndpoint(size_t index)
{
#ifdef BUILD_CLOUD_FEATURES
    return index < g_cloud_endpoint_count ? &g_network_cfg.cloud[index] : nullptr;
#else
    return NULL;
#endif
}

WifiManager::LoadResult WifiManager::lastLoadResult()
{
    return g_last_load_result;
}

const char* WifiManager::lastLoadResultName()
{
    return load_result_tostring(g_last_load_result);
}

namespace
{

// Supporting Functions
// -----------------------------------------------------------------------------

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
    cfg.magic          = kNetworkConfigMagic;
    cfg.version        = kNetworkConfigVersion;
    cfg.security_level = BUILD_WITH_SECURITY_LEVEL;
    strlcpy(cfg.timezone, kDefaultTimezone, sizeof(cfg.timezone));
    for (size_t i = 0; i < kNetworkConfigNtpServerCount; ++i)
    {
        strlcpy(cfg.ntp_server[i], kDefaultNtpServers[i], sizeof(cfg.ntp_server[i]));
    }
}

void sanitize_network_config(network_cfg_t& cfg)
{
    cfg.magic                              = kNetworkConfigMagic;
    cfg.version                            = kNetworkConfigVersion;
    cfg.security_level                     = BUILD_WITH_SECURITY_LEVEL;
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

    cfg.station_count = cfg.station_count > kNetworkConfigMaxEntries ? static_cast<uint8_t>(kNetworkConfigMaxEntries)
                                                                     : cfg.station_count;
    cfg.access_point_count = cfg.access_point_count > kNetworkConfigAllowedEntriesAP
                                 ? static_cast<uint8_t>(kNetworkConfigAllowedEntriesAP)
                                 : cfg.access_point_count;
#ifdef BUILD_CLOUD_FEATURES
    cfg.cloud_endpoint_count = cfg.cloud_endpoint_count > kNetworkConfigCloudAllowedEntries
                                   ? static_cast<uint8_t>(kNetworkConfigCloudAllowedEntries)
                                   : cfg.cloud_endpoint_count;
#else
    cfg.cloud_endpoint_count = 0;
#endif

    for (size_t i = 0; i < kNetworkConfigMaxEntries; ++i)
    {
        cfg.station[i].ssid[sizeof(cfg.station[i].ssid) - 1]         = '\0';
        cfg.station[i].password[sizeof(cfg.station[i].password) - 1] = '\0';
        if (cfg.station[i].icon >= ICON_LAST)
        {
            cfg.station[i].icon = ICON_UNKNOWN;
        }
    }

    for (size_t i = 0; i < kNetworkConfigCloudMaxEntries; ++i)
    {
        cfg.cloud[i].password[sizeof(cfg.cloud[i].password) - 1] = '\0';
        cfg.cloud[i].url[sizeof(cfg.cloud[i].url) - 1]           = '\0';
        if (cfg.cloud[i].icon >= ICON_LAST)
        {
            cfg.cloud[i].icon = ICON_UNKNOWN;
        }
    }

    for (size_t i = 0; i < kNetworkConfigMaxEntriesAP; ++i)
    {
        cfg.access_point[i].ssid[sizeof(cfg.access_point[i].ssid) - 1]         = '\0';
        cfg.access_point[i].password[sizeof(cfg.access_point[i].password) - 1] = '\0';
        if (cfg.access_point[i].icon >= ICON_LAST)
        {
            cfg.access_point[i].icon = ICON_UNKNOWN;
        }
    }
}

void apply_timezone(const char* timezone)
{
    const char* value = timezone && timezone[0] != '\0' ? timezone : kDefaultTimezone;
    setenv("TZ", value, 1);
    tzset();
    DBG_LOGI(TAG, "applied timezone %s", value);
}

bool cache_network_config_hash(const network_cfg_t& cfg)
{
    if (Aegis::cacheNetworkConfigHash(&cfg, sizeof(cfg)))
    {
        return true;
    }

    DBG_LOGW(TAG, "could not cache Wi-Fi config hash");
    return false;
}

bool parse_network_wifi_array(JsonDocument& doc,
                              const char*   key,
                              wifi_item_t*  items,
                              size_t        item_capacity,
                              uint8_t&      count,
                              uint8_t       default_icon,
                              size_t&       skipped)
{
    count           = 0;
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

            JsonObject  item_json = value.as<JsonObject>();
            const char* ssid      = item_json["ssid"].as<const char*>();
            const char* password  = item_json["password"].as<const char*>();
            const char* icon      = item_json["icon"].as<const char*>();
            if (item_json.isNull() || !copy_config_text(items[count].ssid, sizeof(items[count].ssid), ssid, true) ||
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
#ifdef BUILD_CLOUD_FEATURES
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

            JsonObject  item_json = value.as<JsonObject>();
            const char* url       = item_json["url"].as<const char*>();
            const char* password  = item_json["password"].as<const char*>();
            const char* icon      = item_json["icon"].as<const char*>();
            if (item_json.isNull() || !copy_config_text(items[count].url, sizeof(items[count].url), url, true))
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
#else
    (void)doc;
    (void)skipped;
#endif

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

    parse_network_wifi_array(doc,
                             "stations",
                             cfg.station,
                             kNetworkConfigMaxEntries,
                             cfg.station_count,
                             ICON_UNKNOWN,
                             skipped);
    parse_network_wifi_array(doc,
                             "access_points",
                             cfg.access_point,
                             kNetworkConfigAllowedEntriesAP,
                             cfg.access_point_count,
                             ICON_UNKNOWN,
                             skipped);
#ifdef BUILD_CLOUD_FEATURES
    parse_network_cloud_array(doc, cfg.cloud, cfg.cloud_endpoint_count, skipped);
#endif
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

const wifi_item_t* active_wifi_ptr()
{
    return g_has_active_wifi ? &g_active_wifi : nullptr;
}

const wifi_item_t* connected_wifi_ptr()
{
    return g_has_connected_wifi ? &g_connected_wifi : nullptr;
}

void set_active_wifi(const wifi_item_t* item, bool generated_soft_ap)
{
    if (!item)
    {
        clear_active_wifi();
        return;
    }

    g_active_wifi         = *item;
    g_has_active_wifi     = true;
    g_active_generated_ap = generated_soft_ap;
}

void clear_active_wifi()
{
    memset(&g_active_wifi, 0, sizeof(g_active_wifi));
    g_has_active_wifi     = false;
    g_active_generated_ap = false;
}

void set_connected_wifi(const wifi_item_t* item)
{
    if (!item)
    {
        clear_connected_wifi();
        return;
    }

    g_connected_wifi     = *item;
    g_has_connected_wifi = true;
}

void clear_connected_wifi()
{
    memset(&g_connected_wifi, 0, sizeof(g_connected_wifi));
    g_has_connected_wifi = false;
}

void shutdown_for_wifi_activation()
{
    AsyncFsManager::init();

    const BtManager::Result bt_result = BtManager::shutdown();
    if (bt_result != BtManager::Result::Ok)
    {
        DBG_LOGW(TAG, "Bluetooth shutdown before Wi-Fi returned: %s", BtManager::resultName(bt_result));
    }

    AudioManager::stop();
    if (!AudioFileRecorder::stopRecording())
    {
        DBG_LOGW(TAG, "audio file recorder did not stop cleanly before Wi-Fi activation");
    }
    AudioFileRecorder::releaseAudioBuffers();
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

    snprintf(password, password_size, "%08lu", static_cast<unsigned long>(generate_8_digit_nonce()));
}

void configure_soft_ap_security(wifi_config_t& config, const char* ssid, const char* password, uint8_t channel)
{
    memset(&config, 0, sizeof(config));

    strlcpy(reinterpret_cast<char*>(config.ap.ssid), ssid, sizeof(config.ap.ssid));
    strlcpy(reinterpret_cast<char*>(config.ap.password), password, sizeof(config.ap.password));

    config.ap.ssid_len         = strlen(ssid);
    config.ap.channel          = channel;
    config.ap.ssid_hidden      = kSoftApSsidHidden;
    config.ap.authmode         = WIFI_AUTH_WPA3_PSK;
    config.ap.max_connection   = kSoftApMaxConnection;
    config.ap.pmf_cfg.capable  = true;
    config.ap.pmf_cfg.required = true;
#if defined(WIFI_CIPHER_TYPE_CCMP)
    config.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP; // if not set, WPA3 should still require modern cipher behavior
#endif
#if defined(WPA3_SAE_PWE_BOTH)
    config.ap.sae_pwe_h2e =
        WPA3_SAE_PWE_BOTH; // if missing, possible client compatibility issue, not obvious insecurity
#endif
    // security review of these optional settings: all 4 configurations are safe
}

bool start_secure_soft_ap(const char* ssid, const char* password)
{
    const uint8_t channel = get_random_wifi_channel();

    // we are using low level calls to start the Wi-Fi AP so that the security settings are applied before it starts
    // do not use Arduino's built-in Wi-Fi library start-up, for this reason

    wifi_config_t config = {};
    configure_soft_ap_security(config, ssid, password, channel);

    WiFi.mode(WIFI_OFF);
    if (!wifiLowLevelInit(false))
    {
        DBG_LOGW(TAG, "could not initialize Wi-Fi before SoftAP security configuration");
        return false;
    }

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED)
    {
        DBG_LOGW(TAG, "could not stop Wi-Fi before SoftAP security configuration: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK)
    {
        DBG_LOGW(TAG, "could not set Wi-Fi access point mode: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err != ESP_OK)
    {
        DBG_LOGW(TAG, "could not configure WPA3-only SoftAP: %s", esp_err_to_name(err));
        return false;
    }

    // Use Arduino only for the final start so its mode/IP/disconnect helpers
    // stay in sync after the low-level, config-first setup above.
    if (!WiFi.mode(WIFI_MODE_AP))
    {
        DBG_LOGW(TAG, "could not start WPA3-only SoftAP");
        return false;
    }

    DBG_LOGI(TAG, "started WPA3-only SoftAP on channel %u", static_cast<unsigned>(channel));
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

uint8_t get_random_wifi_channel()
{
    if (kSoftApChannelCount == 0)
    {
        return kSoftApFallbackChannel;
    }

    uint32_t random_value = esp_random();
    random_value ^= static_cast<uint32_t>(millis());
    random_value ^= static_cast<uint32_t>(micros()) * 2654435761UL;

    const size_t index = static_cast<size_t>(random_value % kSoftApChannelCount);
    return index < kSoftApChannelCount ? kSoftApChannels[index] : kSoftApFallbackChannel;
}

} // namespace
