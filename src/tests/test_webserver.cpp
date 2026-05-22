#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>

#include "FtpServer.h"
#include "MicroSdCard.h"
#include "WebServer.h"
#include "WifiManager.h"

namespace
{

constexpr const char* TAG = "test_webserver";
constexpr const char* FTP_USER = "thefly";
constexpr const char* FTP_PASSWORD = "replace-me";

void idle_forever()
{
    while (true)
    {
        FtpServer::poll();
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

    WifiManager wifi_manager;
    if (!wifi_manager.startGeneratedSoftAp())
    {
        Serial.printf("%s: generated SoftAP start failed: %s\n", TAG, wifi_manager.statusName());
        idle_forever();
    }

    Serial.printf("%s: SoftAP ssid=\"%s\" password=\"%s\" ip=%s\n",
                  TAG,
                  wifi_manager.generatedSoftApSsid() ? wifi_manager.generatedSoftApSsid() : "",
                  wifi_manager.softApPassword() ? wifi_manager.softApPassword() : "",
                  WiFi.softAPIP().toString().c_str());

    if (!WebServer::init())
    {
        Serial.printf("%s: web server init failed\n", TAG);
        idle_forever();
    }

    // These credentials are only a login gate for plain FTP. Replace them
    // with device/user-specific credentials before exposing FTP. This library
    // is not SFTP and does not encrypt control, credentials, or file data.
    if (!FtpServer::start(MicroSdCard::fs(), FTP_USER, FTP_PASSWORD))
    {
        Serial.printf("%s: FTP server init failed\n", TAG);
        idle_forever();
    }

    Serial.printf("%s: HTTP and FTP servers ready, FTP user=\"%s\", spinning forever\n", TAG, FTP_USER);
    idle_forever();
}
