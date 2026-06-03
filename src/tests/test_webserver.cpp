#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>

#include "MicroSdCard.h"
#include "WebServer.h"
#include "WifiManager.h"

namespace
{

constexpr const char* TAG = "test_webserver";

void idle_forever()
{
    while (true)
    {
        WifiManager::poll();
        delay(10);
    }
}

} // namespace

void test_webserver()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.printf("%s: starting web server test\n", TAG);

    auto cfg         = M5.config();
    cfg.internal_mic = false;
    cfg.internal_spk = false;
    M5.begin(cfg);

    const bool sd_ready = MicroSdCard::begin();
    Serial.printf("%s: microSD ready=%u\n", TAG, sd_ready ? 1U : 0U);

    WifiManager::clear();
    if (!WifiManager::startGeneratedSoftAp())
    {
        Serial.printf("%s: generated SoftAP start failed: %s\n", TAG, WifiManager::statusName());
        idle_forever();
    }

    Serial.printf("%s: SoftAP ssid=\"%s\" password=\"%s\" ip=%s\n",
                  TAG,
                  WifiManager::generatedSoftApSsid() ? WifiManager::generatedSoftApSsid() : "",
                  WifiManager::softApPassword() ? WifiManager::softApPassword() : "",
                  WiFi.softAPIP().toString().c_str());

    if (!WebServer::init())
    {
        Serial.printf("%s: web server init failed\n", TAG);
        idle_forever();
    }

    Serial.printf("%s: HTTP server ready, spinning forever\n", TAG);
    idle_forever();
}
