#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <stdlib.h>

#include "AudioManager.h"
#include "BluetoothManager.h"
#include "esp_bt_defs.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG = "test_btspeakerphone";
constexpr const char* kApSsid = "TheFly-BT-Web-Test";
constexpr const char* kHelloWorld = "hello world\n";

AsyncWebServer g_web_server(80);

void print_local_bdaddr()
{
    esp_bd_addr_t bda = {};
    const esp_err_t err = esp_read_mac(bda, ESP_MAC_BT);
    if (err != ESP_OK)
    {
        Serial.printf("%s: failed to read local Bluetooth address: %s\n", TAG, esp_err_to_name(err));
        return;
    }

    log_bda("local Bluetooth address", bda);
}

bool init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        err = nvs_flash_erase();
        if (err != ESP_OK)
        {
            Serial.printf("%s: nvs erase failed: %s\n", TAG, esp_err_to_name(err));
            return false;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK)
    {
        Serial.printf("%s: nvs init failed: %s\n", TAG, esp_err_to_name(err));
        return false;
    }

    Serial.printf("%s: NVS initialized\n", TAG);
    return true;
}

void on_state_changed(BtManager::State state)
{
    Serial.printf("%s: Bluetooth state: %s\n", TAG, BtManager::stateName(state));
}

bool get_first_bonded_device(esp_bd_addr_t first_device)
{
    const int bonded_count = esp_bt_gap_get_bond_device_num();
    if (bonded_count == ESP_ERR_INVALID_STATE)
    {
        Serial.printf("%s: esp_bt_gap_get_bond_device_num failed: %s\n", TAG, esp_err_to_name(static_cast<esp_err_t>(bonded_count)));
        return false;
    }

    Serial.printf("%s: bonded device count: %d\n", TAG, bonded_count);
    if (bonded_count <= 0)
    {
        return false;
    }

    esp_bd_addr_t* bonded = static_cast<esp_bd_addr_t*>(calloc(bonded_count, sizeof(esp_bd_addr_t)));
    if (!bonded)
    {
        Serial.printf("%s: failed to allocate bonded device list\n", TAG);
        return false;
    }

    int listed = bonded_count;
    const esp_err_t err = esp_bt_gap_get_bond_device_list(&listed, bonded);
    if (err != ESP_OK)
    {
        Serial.printf("%s: esp_bt_gap_get_bond_device_list failed: %s\n", TAG, esp_err_to_name(err));
        free(bonded);
        return false;
    }

    for (int i = 0; i < listed; ++i)
    {
        Serial.printf("%s: bonded[%d]\n", TAG, i);
        log_bda("bonded device", bonded[i]);
    }

    const bool have_device = listed > 0;
    if (have_device)
    {
        memcpy(first_device, bonded[0], ESP_BD_ADDR_LEN);
    }

    free(bonded);
    return have_device;
}

void choke_file_fifos()
{
    AudioManager::bluetoothToFileFifo().clear();
    AudioManager::micToFileFifo().clear();
    AudioManager::bluetoothToFileFifo().choke();
    AudioManager::micToFileFifo().choke();
    Serial.printf("%s: file FIFOs choked\n", TAG);
}

#if 0
bool start_test_web_ap()
{
    WiFi.mode(WIFI_AP);
    const bool ap_started = WiFi.softAP(kApSsid);
    if (!ap_started)
    {
        Serial.printf("%s: Wi-Fi AP start failed\n", TAG);
        return false;
    }

    g_web_server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", kHelloWorld);
    });
    g_web_server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", kHelloWorld);
    });
    g_web_server.begin();

    Serial.printf("%s: Wi-Fi AP started: ssid=\"%s\" ip=%s\n", TAG, kApSsid, WiFi.softAPIP().toString().c_str());
    return true;
}
#endif

} // namespace

void test_btspeakerphone()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.printf("%s: starting Bluetooth speakerphone test\n", TAG);

    if (!init_nvs())
    {
        idle_forever();
    }

    BtManager::generateLegacyPinFromMac();
    Serial.printf("%s: legacy pairing PIN: %s\n", TAG, BtManager::generatedLegacyPin());
    print_local_bdaddr();
    BtManager::setStateChangedCallback(on_state_changed);

    Serial.printf("%s: initializing BluetoothManager\n", TAG);
    if (!BtManager::init(nullptr, AudioManager::hfp_incoming_audio, AudioManager::hfp_outgoing_audio, BtManager::generatedLegacyPin()))
    {
        Serial.printf("%s: BluetoothManager init failed\n", TAG);
        idle_forever();
    }

    esp_bd_addr_t target = {};
    if (!get_first_bonded_device(target))
    {
        Serial.printf("%s: no bonded devices, nothing to connect\n", TAG);
        idle_forever();
    }

    Serial.printf("%s: initializing AudioManager\n", TAG);
    if (!AudioManager::init(AudioManager::Hardware::M5StackInternal))
    {
        Serial.printf("%s: AudioManager init failed\n", TAG);
        idle_forever();
    }

    choke_file_fifos();
    AudioManager::setVolume(AudioManager::kMaxVolume);

    Serial.printf("%s: enabling internal speaker\n", TAG);
    if (!AudioManager::enableSpeakerMode())
    {
        Serial.printf("%s: internal speaker init failed\n", TAG);
        idle_forever();
    }

    log_bda("connecting HFP to", target);

    const BtManager::Result result = BtManager::connectToMac(target);
    Serial.printf("%s: connectToMac: %s\n", TAG, BtManager::resultName(result));
    if (result != BtManager::Result::Ok)
    {
        idle_forever();
    }

    #if 0
    if (!start_test_web_ap())
    {
        idle_forever();
    }
    #endif

    Serial.printf("%s: pumping Bluetooth audio to speaker forever\n", TAG);
    while (true)
    {
        AudioManager::pump_bt2spk();
        taskYIELD();
    }
}
