#pragma once

#include "thefly_common.h"

#include <stddef.h>
#include <stdint.h>

static constexpr size_t kNetworkConfigMaxEntries          = 8;
static constexpr size_t kNetworkConfigMaxEntriesAP        = SOFTAP_CUSTOM_CFG_CNT_MAX;
static constexpr size_t kNetworkConfigAllowedEntriesAP    = SOFTAP_CUSTOM_CFG_CNT;
static constexpr size_t kNetworkConfigCloudMaxEntries     = CLOUD_SERVER_CNT_MAX;
static constexpr size_t kNetworkConfigCloudAllowedEntries = CLOUD_SERVER_CNT_ALLOWED;
static constexpr size_t kNetworkConfigNtpServerCount      = 3;
static constexpr size_t kNetworkConfigSsidMaxLength       = 33;  // 32-byte 802.11 SSID plus NUL
static constexpr size_t kNetworkConfigPasswordMaxLength   = 64;  // 63-byte WPA/WPA2/WPA3 passphrase plus NUL
static constexpr size_t kNetworkConfigTimezoneMaxLength   = 64;
static constexpr size_t kNetworkConfigNtpServerMaxLength  = 254; // DNS name max length plus NUL
static constexpr size_t kNetworkConfigCloudUrlMaxLength   = 256;

typedef struct
{
    char    ssid[kNetworkConfigSsidMaxLength];
    char    password[kNetworkConfigPasswordMaxLength];
    uint8_t icon;      // one of the ICON_* enums
}
wifi_item_t;

typedef struct
{
    char    url[kNetworkConfigCloudUrlMaxLength];
    char    password[kNetworkConfigPasswordMaxLength]; // this is only used in security level 0, but it is kept in memory for all other security levels, just always blank
    uint8_t icon;      // one of the ICON_* enums
}
cloud_item_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint8_t  security_level; // do not load if firmware is mismatched
    char     timezone[kNetworkConfigTimezoneMaxLength];
    char     ntp_server[kNetworkConfigNtpServerCount][kNetworkConfigNtpServerMaxLength];
    uint8_t  station_count;
    uint8_t  access_point_count;
    uint8_t  cloud_endpoint_count;
    wifi_item_t  station[kNetworkConfigMaxEntries];
    wifi_item_t  access_point[kNetworkConfigMaxEntriesAP];
    cloud_item_t cloud[kNetworkConfigCloudMaxEntries];
}
network_cfg_t;

class WifiManager
{
public:
    static constexpr size_t kNtpServerCount             = kNetworkConfigNtpServerCount;
    static constexpr size_t kGeneratedSoftApSsidLength = 12;
    static constexpr size_t kGeneratedSoftApPasswordLength = 8;

    enum class LoadResult
    {
        Ok,
        SdNotReady,
        FileOpenFailed,
        FileTooLarge,
        FileReadFailed,
        FileWriteFailed,
        JsonParseFailed,
        AllocationFailed,
        InvalidItem,
    };

    enum class Status
    {
        Idle,
        StationScanning,
        StationConnecting,
        StationConnected,
        AccessPoint,
        NoKnownNetwork,
        ScanFailed,
        ConnectFailed,
        AccessPointFailed,
    };

    using ConnectionCallback = void (*)(const wifi_item_t* item);
    using ScanFinishedCallback = void (*)(const wifi_item_t* item);

    WifiManager();
    ~WifiManager();

    WifiManager(const WifiManager&)            = delete;
    WifiManager& operator=(const WifiManager&) = delete;

    #if BUILD_WITH_SECURITY_LEVEL <= 0
    bool loadFromMicroSd(const char* path = "/wifi.json");
    #endif
    bool loadFromNvs();
    bool saveToNvs();
    bool copyConfig(network_cfg_t& out) const;
    bool replaceConfig(const network_cfg_t& config);
    void clear();

    const char* timezone() const;
    const char* ntpServer(size_t index) const;

    size_t       stationCount() const;
    wifi_item_t* station(size_t index);
    const wifi_item_t* station(size_t index) const;

    size_t       accessPointCount() const;
    wifi_item_t* accessPoint(size_t index);
    const wifi_item_t* accessPoint(size_t index) const;

    bool connectToHotspot(const wifi_item_t* hotspot);
    bool startSoftAp(const wifi_item_t* access_point);
    bool startGeneratedSoftAp();
    bool scanAndConnect();
    bool disconnect();
    void poll();
    Status status() const;
    const char* statusName() const;
    void setOnConnectCallback(ConnectionCallback callback);
    void setOnDisconnectCallback(ConnectionCallback callback);
    void setOnScanFinished(ScanFinishedCallback callback);
    const wifi_item_t* activeWifi() const;
    const wifi_item_t* connectedWifi() const;
    bool isGeneratedSoftApActive() const;
    const char* generatedSoftApSsid() const;
    const char* softApPassword() const;
    bool softApClientMac(uint8_t out[6]) const;
    uint32_t softApClientConnectionCount() const;
    void noteWebPageLoad();
    void noteWebLogin();
    void noteWebSave();
    void noteWebError();
    void noteWebDownload();
    uint32_t webPageLoadCount() const;
    uint32_t webLoginCount() const;
    uint32_t webSaveCount() const;
    uint32_t webErrorCount() const;
    uint32_t webDownloadCount() const;

    size_t        cloudEndpointCount() const;
    cloud_item_t* cloudEndpoint(size_t index);
    const cloud_item_t* cloudEndpoint(size_t index) const;

    LoadResult lastLoadResult() const;
    const char* lastLoadResultName() const;

private:
    bool connectToHotspot(const wifi_item_t* hotspot, bool shutdown_first);
    void resetWebCounters();
    void resetSoftApClientTracking();
    void updateSoftApClientTracking();
    void notifyConnected(const wifi_item_t* item);
    void notifyDisconnected(const wifi_item_t* item);
    void notifyScanFinished(const wifi_item_t* item);

    network_cfg_t m_network_cfg                    = {};
    size_t        m_station_count                  = 0;
    size_t        m_access_point_count             = 0;
    size_t        m_cloud_endpoint_count           = 0;
    LoadResult    m_last_load_result               = LoadResult::Ok;
    Status        m_status                         = Status::Idle;
    const wifi_item_t* m_active_wifi               = nullptr;
    const wifi_item_t* m_connected_wifi            = nullptr;
    bool          m_reported_connected             = false;
    ConnectionCallback m_on_connect                = nullptr;
    ConnectionCallback m_on_disconnect             = nullptr;
    ScanFinishedCallback m_on_scan_finished        = nullptr;
    char          m_generated_soft_ap_ssid[kGeneratedSoftApSsidLength + 1] = {};
    char          m_generated_soft_ap_password[kGeneratedSoftApPasswordLength + 1] = {};
    wifi_item_t   m_generated_soft_ap              = {};
    uint32_t      m_web_page_load_count            = 0;
    uint32_t      m_web_login_count                = 0;
    uint32_t      m_web_save_count                 = 0;
    uint32_t      m_web_error_count                = 0;
    uint32_t      m_web_download_count             = 0;
    bool          m_soft_ap_client_connected       = false;
    uint8_t       m_soft_ap_client_mac[6]          = {};
    uint32_t      m_soft_ap_client_connection_count = 0;
};
