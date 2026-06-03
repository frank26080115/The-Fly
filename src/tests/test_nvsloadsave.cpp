#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "BtHostList.h"
#include "WifiManager.h"
#include "esp_err.h"
#include "nvs.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG = "test_nvsloadsave";

// Keep the large diagnostic objects at file scope so their zero-initialized
// backing storage lives in .bss instead of the Arduino setup task stack.
network_cfg_t  g_network_report_cfg = {};
network_cfg_t  g_max_network_cfg    = {};
bt_host_list_t g_bt_report_cfg      = {};
BtHostList     g_bt_hosts;
BtHostList     g_bt_verify;

const char* safe_text(const char* text)
{
    return text ? text : "";
}

void fill_alphabet(char* dst, size_t dst_size, size_t offset)
{
    if (!dst || dst_size == 0)
    {
        return;
    }

    for (size_t i = 0; i + 1 < dst_size; ++i)
    {
        dst[i] = static_cast<char>('A' + ((i + offset) % 26));
    }
    dst[dst_size - 1] = '\0';
}

void print_nvs_stats(const char* label)
{
    nvs_stats_t     stats = {};
    const esp_err_t err   = nvs_get_stats(nullptr, &stats);
    if (err != ESP_OK)
    {
        Serial.printf("%s: NVS stats %s failed: %s\n", TAG, safe_text(label), esp_err_to_name(err));
        return;
    }

    Serial.printf("%s: NVS stats %s: used=%u free=%u total=%u namespaces=%u\n",
                  TAG,
                  safe_text(label),
                  static_cast<unsigned>(stats.used_entries),
                  static_cast<unsigned>(stats.free_entries),
                  static_cast<unsigned>(stats.total_entries),
                  static_cast<unsigned>(stats.namespace_count));
}

void print_wifi_item(const char* list_name, size_t index, const wifi_item_t& item)
{
    Serial.printf("%s: %s[%u] ssid=\"%s\" password=\"%s\" icon=%u\n",
                  TAG,
                  list_name,
                  static_cast<unsigned>(index),
                  safe_text(item.ssid),
                  safe_text(item.password),
                  static_cast<unsigned>(item.icon));
}

void print_cloud_item(size_t index, const cloud_item_t& item)
{
    Serial.printf("%s: cloud[%u] url=\"%s\" password=\"%s\" icon=%u\n",
                  TAG,
                  static_cast<unsigned>(index),
                  safe_text(item.url),
                  safe_text(item.password),
                  static_cast<unsigned>(item.icon));
}

void print_bt_host_item(size_t index, const bt_host_item_t& item)
{
    char bdaddr[18] = {};
    format_bdaddr(item.bdaddr, bdaddr, sizeof(bdaddr));
    Serial.printf("%s: bt_host[%u] bdaddr=%s name_custom=\"%s\" name_reported=\"%s\" display=\"%s\" bonded=%u "
                  "last_used=%ld icon=%u\n",
                  TAG,
                  static_cast<unsigned>(index),
                  bdaddr,
                  safe_text(item.name_custom),
                  safe_text(item.name_reported),
                  safe_text(bt_host_display_name(item)),
                  item.bonded ? 1U : 0U,
                  static_cast<long>(item.last_used),
                  static_cast<unsigned>(item.icon));
}

void print_network_config(const char* label)
{
    network_cfg_t& cfg = g_network_report_cfg;
    memset(&cfg, 0, sizeof(cfg));
    WifiManager::copyConfig(cfg);

    Serial.printf("%s: %s WifiManager result=%s sizeof(network_cfg_t)=%u\n",
                  TAG,
                  safe_text(label),
                  WifiManager::lastLoadResultName(),
                  static_cast<unsigned>(sizeof(network_cfg_t)));
    Serial.printf("%s: %s network magic=0x%08lX version=%lu security_level=%u station_count=%u access_point_count=%u "
                  "cloud_endpoint_count=%u\n",
                  TAG,
                  safe_text(label),
                  static_cast<unsigned long>(cfg.magic),
                  static_cast<unsigned long>(cfg.version),
                  static_cast<unsigned>(cfg.security_level),
                  static_cast<unsigned>(cfg.station_count),
                  static_cast<unsigned>(cfg.access_point_count),
                  static_cast<unsigned>(cfg.cloud_endpoint_count));
    Serial.printf("%s: %s timezone=\"%s\"\n", TAG, safe_text(label), safe_text(cfg.timezone));

    for (size_t i = 0; i < kNetworkConfigNtpServerCount; ++i)
    {
        Serial.printf("%s: %s ntp_server[%u]=\"%s\"\n",
                      TAG,
                      safe_text(label),
                      static_cast<unsigned>(i),
                      safe_text(cfg.ntp_server[i]));
    }
    for (size_t i = 0; i < kNetworkConfigMaxEntries; ++i)
    {
        print_wifi_item("station", i, cfg.station[i]);
    }
    for (size_t i = 0; i < kNetworkConfigMaxEntriesAP; ++i)
    {
        print_wifi_item("access_point", i, cfg.access_point[i]);
    }
    for (size_t i = 0; i < kNetworkConfigCloudMaxEntries; ++i)
    {
        print_cloud_item(i, cfg.cloud[i]);
    }
}

void print_bt_host_list(const char* label, const BtHostList& hosts)
{
    bt_host_list_t& cfg = g_bt_report_cfg;
    memset(&cfg, 0, sizeof(cfg));
    hosts.copyHostList(cfg);

    Serial.printf("%s: %s BtHostList result=%s sizeof(bt_host_list_t)=%u count=%u\n",
                  TAG,
                  safe_text(label),
                  hosts.lastLoadResultName(),
                  static_cast<unsigned>(sizeof(bt_host_list_t)),
                  static_cast<unsigned>(cfg.count));
    Serial.printf("%s: %s bluetooth magic=0x%08lX version=%lu\n",
                  TAG,
                  safe_text(label),
                  static_cast<unsigned long>(cfg.magic),
                  static_cast<unsigned long>(cfg.version));

    for (size_t i = 0; i < kBtHostListMaxEntries; ++i)
    {
        print_bt_host_item(i, cfg.host[i]);
    }
}

void fill_max_network_config(network_cfg_t& cfg)
{
    memset(&cfg, 0, sizeof(cfg));
    cfg.security_level       = BUILD_WITH_SECURITY_LEVEL;
    cfg.station_count        = static_cast<uint8_t>(kNetworkConfigMaxEntries);
    cfg.access_point_count   = static_cast<uint8_t>(kNetworkConfigMaxEntriesAP);
    cfg.cloud_endpoint_count = static_cast<uint8_t>(kNetworkConfigCloudMaxEntries);

    fill_alphabet(cfg.timezone, sizeof(cfg.timezone), 0);
    for (size_t i = 0; i < kNetworkConfigNtpServerCount; ++i)
    {
        fill_alphabet(cfg.ntp_server[i], sizeof(cfg.ntp_server[i]), i);
    }
    for (size_t i = 0; i < kNetworkConfigMaxEntries; ++i)
    {
        fill_alphabet(cfg.station[i].ssid, sizeof(cfg.station[i].ssid), i);
        fill_alphabet(cfg.station[i].password, sizeof(cfg.station[i].password), i + 1);
        cfg.station[i].icon = static_cast<uint8_t>(ICON_SMARTPHONE + (i % (ICON_LAST - ICON_SMARTPHONE)));
    }
    for (size_t i = 0; i < kNetworkConfigMaxEntriesAP; ++i)
    {
        fill_alphabet(cfg.access_point[i].ssid, sizeof(cfg.access_point[i].ssid), i + 2);
        fill_alphabet(cfg.access_point[i].password, sizeof(cfg.access_point[i].password), i + 3);
        cfg.access_point[i].icon = static_cast<uint8_t>(ICON_HOME);
    }
    for (size_t i = 0; i < kNetworkConfigCloudMaxEntries; ++i)
    {
        fill_alphabet(cfg.cloud[i].url, sizeof(cfg.cloud[i].url), i + 4);
        fill_alphabet(cfg.cloud[i].password, sizeof(cfg.cloud[i].password), i + 5);
        cfg.cloud[i].icon = static_cast<uint8_t>(ICON_CIRCLE + (i % (ICON_LAST - ICON_CIRCLE)));
    }
}

void fill_max_bt_host_list(BtHostList& hosts)
{
    hosts.clear();
    for (size_t i = 0; i < kBtHostListMaxEntries; ++i)
    {
        esp_bd_addr_t bdaddr = {
            0x02,
            0x46,
            0x4C,
            0x59,
            static_cast<uint8_t>(0x10 + i),
            static_cast<uint8_t>(0x80 + i),
        };

        char reported_name[kBtHostNameMaxLength] = {};
        fill_alphabet(reported_name, sizeof(reported_name), i);
        hosts.insert(reported_name,
                     bdaddr,
                     static_cast<uint8_t>(ICON_SMARTPHONE + (i % (ICON_LAST - ICON_SMARTPHONE))));

        bt_host_item_t* item = hosts.get(i);
        if (item)
        {
            fill_alphabet(item->name_custom, sizeof(item->name_custom), i + 1);
            fill_alphabet(item->name_reported, sizeof(item->name_reported), i + 2);
            item->last_used = static_cast<time_t>(1700000000UL + (i * 3600UL));
        }
    }
}

} // namespace

void test_nvsloadsave()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.printf("%s: starting NVS load/save stress test\n", TAG);
    Serial.printf("%s: BUILD_WITH_SECURITY_LEVEL=%d\n", TAG, BUILD_WITH_SECURITY_LEVEL);
    Serial.printf("%s: capacity stations=%u\n", TAG, static_cast<unsigned>(kNetworkConfigMaxEntries));
    Serial.printf("%s: capacity access_points max=%u allowed=%u\n",
                  TAG,
                  static_cast<unsigned>(kNetworkConfigMaxEntriesAP),
                  static_cast<unsigned>(kNetworkConfigAllowedEntriesAP));
    Serial.printf("%s: capacity cloud max=%u allowed=%u\n",
                  TAG,
                  static_cast<unsigned>(kNetworkConfigCloudMaxEntries),
                  static_cast<unsigned>(kNetworkConfigCloudAllowedEntries));
    Serial.printf("%s: capacity bt_hosts=%u\n", TAG, static_cast<unsigned>(kBtHostListMaxEntries));
    Serial.printf("%s: blob sizes: network_cfg_t=%u bt_host_list_t=%u total=%u\n",
                  TAG,
                  static_cast<unsigned>(sizeof(network_cfg_t)),
                  static_cast<unsigned>(sizeof(bt_host_list_t)),
                  static_cast<unsigned>(sizeof(network_cfg_t) + sizeof(bt_host_list_t)));

    print_nvs_stats("before load");

    const bool wifi_loaded = WifiManager::loadFromNvs();
    Serial.printf("%s: WifiManager::loadFromNvs=%u result=%s\n",
                  TAG,
                  wifi_loaded ? 1U : 0U,
                  WifiManager::lastLoadResultName());
    print_network_config("loaded");

    BtHostList& bt_hosts  = g_bt_hosts;
    const bool  bt_loaded = bt_hosts.loadFromNvs();
    Serial.printf("%s: BtHostList::loadFromNvs=%u result=%s\n",
                  TAG,
                  bt_loaded ? 1U : 0U,
                  bt_hosts.lastLoadResultName());
    print_bt_host_list("loaded", bt_hosts);

    network_cfg_t& max_network = g_max_network_cfg;
    fill_max_network_config(max_network);
    fill_max_bt_host_list(bt_hosts);

    Serial.printf("%s: writing maximum-size test data to NVS\n", TAG);
    const bool wifi_saved = WifiManager::replaceConfig(max_network);
    Serial.printf("%s: WifiManager::replaceConfig/save=%u result=%s\n",
                  TAG,
                  wifi_saved ? 1U : 0U,
                  WifiManager::lastLoadResultName());

    const bool bt_saved = bt_hosts.saveToNvs();
    Serial.printf("%s: BtHostList::saveToNvs=%u result=%s\n", TAG, bt_saved ? 1U : 0U, bt_hosts.lastLoadResultName());

    print_nvs_stats("after save");

    const bool wifi_verify_loaded = WifiManager::loadFromNvs();
    Serial.printf("%s: verify WifiManager::loadFromNvs=%u result=%s\n",
                  TAG,
                  wifi_verify_loaded ? 1U : 0U,
                  WifiManager::lastLoadResultName());
    print_network_config("verified");

    BtHostList& bt_verify        = g_bt_verify;
    const bool  bt_verify_loaded = bt_verify.loadFromNvs();
    Serial.printf("%s: verify BtHostList::loadFromNvs=%u result=%s\n",
                  TAG,
                  bt_verify_loaded ? 1U : 0U,
                  bt_verify.lastLoadResultName());
    print_bt_host_list("verified", bt_verify);

    print_nvs_stats("after verify");

    Serial.printf("%s: finished, spinning forever\n", TAG);
    idle_forever();
}
