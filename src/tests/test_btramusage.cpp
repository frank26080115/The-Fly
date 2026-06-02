#include <Arduino.h>
#include <WiFi.h>

#include "BluetoothManager.h"
#include "WebServer.h"
#include "WifiManager.h"
#include "esp_heap_caps.h"
#include "utilfuncs.h"

extern WifiManager* wifi_manager;

namespace
{

constexpr const char* TAG = "test_btramusage";

void report_memory(const char* label)
{
    Serial.printf("%s: memory: %s\n", TAG, label ? label : "");
    Serial.printf("%s:   heap free=%u min_free=%u largest=%u\n",
                  TAG,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    Serial.printf("%s:   internal free=%u min_free=%u largest=%u\n",
                  TAG,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    Serial.printf("%s:   psram free=%u min_free=%u largest=%u\n",
                  TAG,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

void wait_and_poll(uint32_t wait_ms, WifiManager* wifi = nullptr, bool poll_bluetooth = false)
{
    const uint32_t started = millis();
    while (static_cast<uint32_t>(millis() - started) < wait_ms)
    {
        if (poll_bluetooth)
        {
            BtManager::poll();
        }
        if (wifi)
        {
            wifi->poll();
        }
        delay(10);
    }
}

void spin_forever(WifiManager* wifi = nullptr)
{
    while (true)
    {
        if (wifi)
        {
            wifi->poll();
        }
        delay(10);
    }
}

} // namespace

void test_btramusage()
{
    Serial.begin(115200);
    delay(3000);

    Serial.println();
    Serial.printf("%s: starting Bluetooth RAM usage test\n", TAG);
    report_memory("startup");

    BtManager::setStateChangedCallback(
        [](BtManager::State state) { Serial.printf("%s: Bluetooth state: %s\n", TAG, BtManager::stateName(state)); });

    BtManager::generateLegacyPinFromMac();
    Serial.printf("%s: legacy pairing PIN: %s\n", TAG, BtManager::generatedLegacyPin());
    Serial.printf("%s: initializing BluetoothManager\n", TAG);
    if (!BtManager::init(nullptr, nullptr, nullptr, BtManager::generatedLegacyPin()))
    {
        Serial.printf("%s: BluetoothManager init failed\n", TAG);
        report_memory("after failed Bluetooth init");
        idle_forever();
    }

    const BtManager::Result pairing_result = BtManager::startPairing();
    Serial.printf("%s: startPairing: %s\n", TAG, BtManager::resultName(pairing_result));
    wait_and_poll(1000, nullptr, true);
    report_memory("after Bluetooth init + pairing");

    const BtManager::Result shutdown_result = BtManager::shutdown();
    Serial.printf("%s: BtManager::shutdown: %s\n", TAG, BtManager::resultName(shutdown_result));
    wait_and_poll(1000);
    report_memory("after Bluetooth shutdown");

    static WifiManager web_wifi_manager;
    wifi_manager = &web_wifi_manager;
    if (!web_wifi_manager.startGeneratedSoftAp())
    {
        Serial.printf("%s: generated SoftAP start failed: %s\n", TAG, web_wifi_manager.statusName());
        report_memory("after failed SoftAP start");
        spin_forever(&web_wifi_manager);
    }

    Serial.printf("%s: SoftAP ssid=\"%s\" password=\"%s\" ip=%s\n",
                  TAG,
                  web_wifi_manager.generatedSoftApSsid() ? web_wifi_manager.generatedSoftApSsid() : "",
                  web_wifi_manager.softApPassword() ? web_wifi_manager.softApPassword() : "",
                  WiFi.softAPIP().toString().c_str());

    if (!WebServer::init())
    {
        Serial.printf("%s: WebServer init failed\n", TAG);
        report_memory("after failed WebServer init");
        spin_forever(&web_wifi_manager);
    }

    wait_and_poll(1000, &web_wifi_manager);
    report_memory("after SoftAP + WebServer");

    Serial.printf("%s: Bluetooth RAM usage test complete\n", TAG);
    spin_forever(&web_wifi_manager);
}
