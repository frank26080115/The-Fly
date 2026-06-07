#include <Arduino.h>
#include <stdlib.h>

#include "BluetoothManager.h"
#include "esp_bt_defs.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "dbg_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "utilfuncs.h"

extern bool init_nvs();

namespace
{

constexpr const char* TAG = "test_btpairing";

volatile bool g_pairing_finished = false;

void print_local_bdaddr()
{
    esp_bd_addr_t   bda = {};
    const esp_err_t err = esp_read_mac(bda, ESP_MAC_BT);
    if (err != ESP_OK)
    {
        Serial.printf("%s: failed to read local Bluetooth address: %s\n", TAG, esp_err_to_name(err));
        return;
    }

    log_bda("local Bluetooth address", bda);
}

void print_bonded_devices()
{
    const int bonded_count = esp_bt_gap_get_bond_device_num();
    if (bonded_count == ESP_ERR_INVALID_STATE)
    {
        Serial.printf("%s: esp_bt_gap_get_bond_device_num failed: %s\n",
                      TAG,
                      esp_err_to_name(static_cast<esp_err_t>(bonded_count)));
        return;
    }

    Serial.printf("%s: bonded device count: %d\n", TAG, bonded_count);
    if (bonded_count <= 0)
    {
        return;
    }

    esp_bd_addr_t* bonded = static_cast<esp_bd_addr_t*>(calloc(bonded_count, sizeof(esp_bd_addr_t)));
    if (!bonded)
    {
        Serial.printf("%s: failed to allocate bonded device list\n", TAG);
        return;
    }

    int             listed = bonded_count;
    const esp_err_t err    = esp_bt_gap_get_bond_device_list(&listed, bonded);
    if (err != ESP_OK)
    {
        Serial.printf("%s: esp_bt_gap_get_bond_device_list failed: %s\n", TAG, esp_err_to_name(err));
        free(bonded);
        return;
    }

    for (int i = 0; i < listed; ++i)
    {
        Serial.printf("%s: bonded[%d]\n", TAG, i);
        log_bda("bonded device", bonded[i]);
    }

    free(bonded);
}

void on_state_changed(BtManager::State state)
{
    Serial.printf("%s: Bluetooth state: %s\n", TAG, BtManager::stateName(state));
}

void on_paired(const BtManager::PairedDevice& device)
{
    DBG_LOGI(TAG, "pairing completed");
    log_bda("paired with", device.mac);
    Serial.printf("%s: paired name=\"%s\"\n", TAG, device.name);
    g_pairing_finished = true;
}

} // namespace

void test_btpairing()
{
    Serial.begin(115200, SERIAL_8N1, -1, 1);
    delay(1000);

    Serial.println();
    Serial.printf("%s: starting Bluetooth pairing test\n", TAG);

    if (!init_nvs())
    {
        idle_forever();
    }

    BtManager::generateLegacyPinFromMac();
    Serial.printf("%s: legacy pairing PIN: %s\n", TAG, BtManager::generatedLegacyPin());
    print_local_bdaddr();
    BtManager::setStateChangedCallback(on_state_changed);
    BtManager::setPairedCallback(on_paired);

    Serial.printf("%s: initializing BluetoothManager\n", TAG);
    if (!BtManager::init(nullptr, nullptr, nullptr, BtManager::generatedLegacyPin()))
    {
        Serial.printf("%s: BluetoothManager init failed\n", TAG);
        idle_forever();
    }

    print_bonded_devices();

    const BtManager::Result result = BtManager::startPairing();
    Serial.printf("%s: startPairing: %s\n", TAG, BtManager::resultName(result));
    if (result != BtManager::Result::Ok)
    {
        idle_forever();
    }

    while (!g_pairing_finished)
    {
        delay(100);
    }

    Serial.printf("%s: ending pairing mode\n", TAG);
    BtManager::disconnect();
    idle_forever();
}
