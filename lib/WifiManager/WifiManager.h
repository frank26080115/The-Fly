#pragma once

#include <stddef.h>
#include <stdint.h>

#include "defs.h"

typedef struct
{
    char*   ssid;      // allocate on creation, free on destructor
    char*   password;  // allocate on creation, free on destructor
    uint8_t icon;      // one of the ICON_* enums
    void*   next_node; // linked list next node
}
wifi_item_t;

typedef struct
{
    char*   name;      // allocate on creation, free on destructor
    char*   url;       // allocate on creation, free on destructor
    char*   password;  // this is a hash salt, never actually send it, allocate on creation, free on destructor
    uint8_t icon;      // one of the ICON_* enums
    void*   next_node; // linked list next node
}
cloud_item_t;

class WifiManager
{
public:
    enum class LoadResult
    {
        Ok,
        SdNotReady,
        FileOpenFailed,
        FileTooLarge,
        FileReadFailed,
        JsonParseFailed,
        AllocationFailed,
        InvalidItem,
    };

    enum class Status
    {
        Idle,
        StationConnecting,
        StationConnected,
        AccessPoint,
        NoKnownNetwork,
        ScanFailed,
        ConnectFailed,
        AccessPointFailed,
    };

    using ConnectionCallback = void (*)(const wifi_item_t* item);

    WifiManager();
    ~WifiManager();

    WifiManager(const WifiManager&)            = delete;
    WifiManager& operator=(const WifiManager&) = delete;

    bool loadFromMicroSd(const char* path = "/wifi.json");
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
    bool scanAndConnect();
    bool disconnect();
    void poll();
    Status status() const;
    const char* statusName() const;
    void setOnConnectCallback(ConnectionCallback callback);
    void setOnDisconnectCallback(ConnectionCallback callback);

    size_t        cloudEndpointCount() const;
    cloud_item_t* cloudEndpoint(size_t index);
    const cloud_item_t* cloudEndpoint(size_t index) const;

    LoadResult lastResult() const;
    const char* lastResultName() const;

private:
    static constexpr size_t kNtpServerCount = 3;

    void notifyConnected(const wifi_item_t* item);
    void notifyDisconnected(const wifi_item_t* item);

    char*         m_timezone                       = nullptr;
    char*         m_ntp_servers[kNtpServerCount]   = {};
    wifi_item_t*  m_station_head                   = nullptr;
    wifi_item_t*  m_station_tail                   = nullptr;
    size_t        m_station_count                  = 0;
    wifi_item_t*  m_access_point_head              = nullptr;
    wifi_item_t*  m_access_point_tail              = nullptr;
    size_t        m_access_point_count             = 0;
    cloud_item_t* m_cloud_endpoint_head            = nullptr;
    cloud_item_t* m_cloud_endpoint_tail            = nullptr;
    size_t        m_cloud_endpoint_count           = 0;
    LoadResult    m_last_result                    = LoadResult::Ok;
    Status        m_status                         = Status::Idle;
    const wifi_item_t* m_active_wifi               = nullptr;
    const wifi_item_t* m_connected_wifi            = nullptr;
    bool          m_reported_connected             = false;
    ConnectionCallback m_on_connect                = nullptr;
    ConnectionCallback m_on_disconnect             = nullptr;
};
