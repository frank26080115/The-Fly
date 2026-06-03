#pragma once

#include "thefly_common.h"

#include <IPAddress.h>
#include <stddef.h>
#include <stdint.h>

static constexpr size_t kNetworkConfigMaxEntries          = 8;
static constexpr size_t kNetworkConfigMaxEntriesAP        = SOFTAP_CUSTOM_CFG_CNT_MAX;
static constexpr size_t kNetworkConfigAllowedEntriesAP    = SOFTAP_CUSTOM_CFG_CNT;
static constexpr size_t kNetworkConfigCloudMaxEntries     = CLOUD_SERVER_CNT_MAX;
static constexpr size_t kNetworkConfigCloudAllowedEntries = CLOUD_SERVER_CNT_ALLOWED;
static constexpr size_t kNetworkConfigNtpServerCount      = 3;
static constexpr size_t kNetworkConfigSsidMaxLength       = 33; // 32-byte 802.11 SSID plus NUL
static constexpr size_t kNetworkConfigPasswordMaxLength   = 64; // 63-byte WPA/WPA2/WPA3 passphrase plus NUL
static constexpr size_t kNetworkConfigTimezoneMaxLength   = 64;
static constexpr size_t kNetworkConfigNtpServerMaxLength  = 254; // DNS name max length plus NUL
static constexpr size_t kNetworkConfigCloudUrlMaxLength   = 256;

typedef struct
{
    char    ssid[kNetworkConfigSsidMaxLength];
    char    password[kNetworkConfigPasswordMaxLength];
    uint8_t icon; // one of the ICON_* enums
} wifi_item_t;

typedef struct
{
    char url[kNetworkConfigCloudUrlMaxLength];
    char password[kNetworkConfigPasswordMaxLength]; // this is only used in security level 0, but it is kept in memory
                                                    // for all other security levels, just always blank
    uint8_t icon;                                   // one of the ICON_* enums
} cloud_item_t;

typedef struct
{
    uint32_t     magic;
    uint32_t     version;
    uint8_t      security_level; // do not load if firmware is mismatched
    char         timezone[kNetworkConfigTimezoneMaxLength];
    char         ntp_server[kNetworkConfigNtpServerCount][kNetworkConfigNtpServerMaxLength];
    uint8_t      station_count;
    uint8_t      access_point_count;
    uint8_t      cloud_endpoint_count;
    wifi_item_t  station[kNetworkConfigMaxEntries];
    wifi_item_t  access_point[kNetworkConfigMaxEntriesAP];
    cloud_item_t cloud[kNetworkConfigCloudMaxEntries];
} network_cfg_t;

namespace WifiManager
{
    static constexpr size_t kNtpServerCount                = kNetworkConfigNtpServerCount;
    static constexpr size_t kGeneratedSoftApSsidLength     = 12;
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

    using ConnectionCallback   = void (*)(const wifi_item_t* item);
    using ScanFinishedCallback = void (*)(const wifi_item_t* item);

#if BUILD_WITH_SECURITY_LEVEL <= 0
    bool loadFromMicroSd(const char* path = "/wifi.json");
#endif
    bool loadFromNvs();
    bool saveToNvs();
    bool copyConfig(network_cfg_t& out);
    bool replaceConfig(const network_cfg_t& config);
    void clear();

    const char* timezone();
    const char* ntpServer(size_t index);

    size_t             stationCount();
    const wifi_item_t* station(size_t index);

    size_t             accessPointCount();
    const wifi_item_t* accessPoint(size_t index);

    bool               connectToHotspot(const wifi_item_t* hotspot);
    bool               startSoftAp(const wifi_item_t* access_point);
    bool               startGeneratedSoftAp();
    bool               scanAndConnect();
    bool               disconnect();
    bool               wifiHasStarted();
    void               poll();
    Status             status();
    const char*        statusName();
    void               setOnConnectCallback(ConnectionCallback callback);
    void               setOnDisconnectCallback(ConnectionCallback callback);
    void               setOnScanFinished(ScanFinishedCallback callback);
    const wifi_item_t* activeWifi();
    const wifi_item_t* connectedWifi();
    bool               isGeneratedSoftApActive();
    const char*        generatedSoftApSsid();
    const char*        softApPassword();
    IPAddress          softApIp();
    bool               softApClientMac(uint8_t out[6]);
    uint32_t           softApClientConnectionCount();
    void               noteWebPageLoad();
    void               noteWebLogin();
    void               noteWebSave();
    void               noteWebError();
    void               noteWebDownload();
    uint32_t           webPageLoadCount();
    uint32_t           webLoginCount();
    uint32_t           webSaveCount();
    uint32_t           webErrorCount();
    uint32_t           webDownloadCount();

    size_t              cloudEndpointCount();
    const cloud_item_t* cloudEndpoint(size_t index);

    LoadResult  lastLoadResult();
    const char* lastLoadResultName();
} // namespace WifiManager
