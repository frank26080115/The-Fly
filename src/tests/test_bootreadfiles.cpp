#include <Arduino.h>
#include <M5Unified.h>

#include "BtHostList.h"
#include "MicroSdCard.h"
#include "WifiManager.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG = "test_bootreadfiles";

const char* safe_text(const char* text)
{
    return text ? text : "";
}

void report_wifi_item(const char* list_name, size_t index, const wifi_item_t* item)
{
    if (!item)
    {
        Serial.printf("%s: %s[%u]=<null>\n", TAG, list_name, static_cast<unsigned>(index));
        return;
    }

    Serial.printf("%s: %s[%u] ssid=\"%s\" password=\"%s\" icon=%u\n",
                  TAG,
                  list_name,
                  static_cast<unsigned>(index),
                  safe_text(item->ssid),
                  safe_text(item->password),
                  static_cast<unsigned>(item->icon));
}

void report_cloud_item(size_t index, const cloud_item_t* item)
{
    if (!item)
    {
        Serial.printf("%s: cloud_endpoint[%u]=<null>\n", TAG, static_cast<unsigned>(index));
        return;
    }

    Serial.printf("%s: cloud_endpoint[%u] url=\"%s\" password=\"%s\" icon=%u\n",
                  TAG,
                  static_cast<unsigned>(index),
                  safe_text(item->url),
                  safe_text(item->password),
                  static_cast<unsigned>(item->icon));
}

void report_bt_host_item(size_t index, const bt_host_item_t* item)
{
    if (!item)
    {
        Serial.printf("%s: bt_host[%u]=<null>\n", TAG, static_cast<unsigned>(index));
        return;
    }

    Serial.printf("%s: bt_host[%u] bdaddr=%02X:%02X:%02X:%02X:%02X:%02X name=\"%s\" bonded=%u icon=%u\n",
                  TAG,
                  static_cast<unsigned>(index),
                  item->bdaddr[0],
                  item->bdaddr[1],
                  item->bdaddr[2],
                  item->bdaddr[3],
                  item->bdaddr[4],
                  item->bdaddr[5],
                  safe_text(bt_host_display_name(item)),
                  item->bonded ? 1U : 0U,
                  static_cast<unsigned>(item->icon));
}

void report_wifi_summary(const WifiManager& wifi_manager, bool loaded)
{
    Serial.printf("%s: WifiManager load=%u result=%s stations=%u access_points=%u cloud_endpoints=%u timezone=%s\n",
                  TAG,
                  loaded ? 1U : 0U,
                  wifi_manager.lastLoadResultName(),
                  static_cast<unsigned>(wifi_manager.stationCount()),
                  static_cast<unsigned>(wifi_manager.accessPointCount()),
                  static_cast<unsigned>(wifi_manager.cloudEndpointCount()),
                  wifi_manager.timezone());

    for (size_t i = 0; i < WifiManager::kNtpServerCount; ++i)
    {
        Serial.printf("%s: ntp_server[%u]=%s\n",
                      TAG,
                      static_cast<unsigned>(i),
                      wifi_manager.ntpServer(i));
    }

    for (size_t i = 0; i < wifi_manager.stationCount(); ++i)
    {
        report_wifi_item("station", i, wifi_manager.station(i));
    }

    for (size_t i = 0; i < wifi_manager.accessPointCount(); ++i)
    {
        report_wifi_item("access_point", i, wifi_manager.accessPoint(i));
    }

    for (size_t i = 0; i < wifi_manager.cloudEndpointCount(); ++i)
    {
        report_cloud_item(i, wifi_manager.cloudEndpoint(i));
    }
}

void report_bt_summary(const BtHostList& bt_hosts, bool loaded)
{
    Serial.printf("%s: BtHostList load=%u result=%s hosts=%u\n",
                  TAG,
                  loaded ? 1U : 0U,
                  bt_hosts.lastLoadResultName(),
                  static_cast<unsigned>(bt_hosts.size()));

    for (size_t i = 0; i < bt_hosts.size(); ++i)
    {
        report_bt_host_item(i, bt_hosts.get(i));
    }
}

} // namespace

void test_bootreadfiles()
{
    Serial.begin(115200);
    delay(1000);

    auto cfg         = M5.config();
    cfg.internal_mic = false;
    cfg.internal_spk = false;
    M5.begin(cfg);

    Serial.println();
    Serial.printf("%s: starting boot file read test. Time (ms): %lu\n", TAG, static_cast<unsigned long>(millis()));

    const bool sd_ready = MicroSdCard::begin();
    Serial.printf("%s: MicroSdCard begin=%u ready=%u\n", TAG, sd_ready ? 1U : 0U, MicroSdCard::isReady() ? 1U : 0U);

    WifiManager wifi_manager;
    #if BUILD_WITH_SECURITY_LEVEL <= 0
    const bool  wifi_loaded = wifi_manager.loadFromMicroSd();
    #else
    const bool  wifi_loaded = wifi_manager.loadFromNvs();
    #endif
    report_wifi_summary(wifi_manager, wifi_loaded);

    BtHostList bt_hosts;
    const bool bt_loaded = bt_hosts.loadFromMicroSd();
    report_bt_summary(bt_hosts, bt_loaded);

    Serial.printf("%s: finished, spinning forever. Time (ms): %lu\n", TAG, static_cast<unsigned long>(millis()));
    idle_forever();
}
