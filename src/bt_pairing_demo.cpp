#include <Arduino.h>
#include <M5Unified.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "utilfuncs.h"

#ifndef BT_PAIRING_DEMO_DEVICE_NAME
#define BT_PAIRING_DEMO_DEVICE_NAME "The Fly Pairing Demo"
#endif

struct BtPairingDemoCredentials
{
    uint32_t version;
    uint8_t  remote_bda[ESP_BD_ADDR_LEN];
    char     remote_name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
    bool     valid;
    // no actual key, ESP-IDF stores keys internally, use `is_bonded` to check
};

namespace
{
constexpr const char* TAG                = "bt_pairing_demo";
constexpr uint32_t    kCredentialVersion = 1;
constexpr uint32_t    kPairingWindowMs   = 60000;
constexpr char        kLegacyPin[]       = "0000";

bool                     g_bt_ready            = false;
bool                     g_hfp_ready           = false;
bool                     g_pairing_window_open = false;
bool                     g_pairing_complete    = false;
BtPairingDemoCredentials g_last_credentials    = {};

bool is_bonded(const uint8_t bda[ESP_BD_ADDR_LEN])
{
    const int count = esp_bt_gap_get_bond_device_num();
    if (count <= 0)
    {
        return false;
    }

    esp_bd_addr_t* bonded = static_cast<esp_bd_addr_t*>(calloc(count, sizeof(esp_bd_addr_t)));
    if (!bonded)
    {
        return false;
    }

    int        listed   = count;
    const bool got_list = esp_bt_gap_get_bond_device_list(&listed, bonded) == ESP_OK;
    bool       found    = false;
    if (got_list)
    {
        for (int i = 0; i < listed; ++i)
        {
            if (bda_equal(bda, bonded[i]))
            {
                found = true;
                break;
            }
        }
    }

    free(bonded);
    return found;
}

void close_pairing_window()
{
    g_pairing_window_open = false;
    ok(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE), "close pairing window");
}

void hfp_event(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t* param)
{
    if (event != ESP_HF_CLIENT_CONNECTION_STATE_EVT || !param)
    {
        return;
    }

    if (param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED)
    {
        log_bda("hfp service connection established with", param->conn_stat.remote_bda);
    }
}

void gap_event(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param)
{
    switch (event)
    {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
        {
            g_last_credentials         = {};
            g_last_credentials.version = kCredentialVersion;
            g_last_credentials.valid   = true;
            copy_bda(g_last_credentials.remote_bda, param->auth_cmpl.bda);
            strlcpy(g_last_credentials.remote_name, reinterpret_cast<const char*>(param->auth_cmpl.device_name), sizeof(g_last_credentials.remote_name));
            g_pairing_complete = true;
            close_pairing_window();
            log_bda("paired with", g_last_credentials.remote_bda);
            ESP_LOGI(TAG, "save version, remote_bda, remote_name, and keep Bluedroid NVS bond data");
        }
        else
        {
            ESP_LOGW(TAG, "pairing failed, status=%d", param->auth_cmpl.stat);
        }
        break;

    case ESP_BT_GAP_PIN_REQ_EVT:
    {
        esp_bt_pin_code_t pin = {};
        memcpy(pin, kLegacyPin, strlen(kLegacyPin));
        ok(esp_bt_gap_pin_reply(param->pin_req.bda, true, strlen(kLegacyPin), pin), "legacy pin reply");
        break;
    }

    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "confirming SSP numeric comparison: %" PRIu32, param->cfm_req.num_val);
        ok(esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true), "ssp confirm reply");
        break;

    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "SSP passkey: %" PRIu32, param->key_notif.passkey);
        break;

    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGW(TAG, "remote requested passkey entry; this demo has no keyboard");
        break;

    default:
        break;
    }
}

bool init_bluetooth()
{
    if (g_bt_ready)
    {
        return true;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ok(nvs_flash_erase(), "nvs erase");
        err = nvs_flash_init();
    }
    if (!ok(err, "nvs init"))
    {
        return false;
    }

    ok(esp_bt_controller_mem_release(ESP_BT_MODE_BLE), "release ble memory");

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (!ok(esp_bt_controller_init(&bt_cfg), "bt controller init") || !ok(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT), "bt controller enable") || !ok(esp_bluedroid_init(), "bluedroid init") || !ok(esp_bluedroid_enable(), "bluedroid enable"))
    {
        return false;
    }

    esp_bt_io_cap_t   iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_pin_code_t pin   = {};
    memcpy(pin, kLegacyPin, strlen(kLegacyPin));

    ok(esp_bt_sleep_disable(), "bt sleep disable");
    ok(esp_bt_gap_register_callback(gap_event), "gap callback");
    ok(esp_bt_dev_set_device_name(BT_PAIRING_DEMO_DEVICE_NAME), "bt name");
    ok(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap)), "ssp iocap");
    ok(esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, strlen(kLegacyPin), pin), "legacy pin");
    ok(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE), "initial scan mode");

    g_bt_ready = true;
    return true;
}

bool init_hfp()
{
    if (g_hfp_ready)
    {
        return true;
    }

    if (!init_bluetooth() || !ok(esp_hf_client_register_callback(hfp_event), "hfp callback") || !ok(esp_hf_client_init(), "hfp init"))
    {
        return false;
    }

    g_hfp_ready = true;
    return true;
}
} // namespace

bool bt_pairing_demo_pair(BtPairingDemoCredentials* saved_credentials)
{
    M5.begin();

    if (!init_hfp())
    {
        return false;
    }

    g_pairing_complete    = false;
    g_pairing_window_open = true;
    g_last_credentials    = {};

    if (!ok(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE), "open pairing window"))
    {
        g_pairing_window_open = false;
        return false;
    }

    const uint32_t started = millis();
    while (!g_pairing_complete && millis() - started < kPairingWindowMs)
    {
        M5.update();
        delay(25);
    }

    if (g_pairing_window_open)
    {
        close_pairing_window();
    }

    if (!g_pairing_complete)
    {
        ESP_LOGW(TAG, "no peer paired within %lu ms", static_cast<unsigned long>(kPairingWindowMs));
        return false;
    }

    if (saved_credentials)
    {
        *saved_credentials = g_last_credentials;
    }

    return true;
}

bool bt_pairing_demo_pair()
{
    return bt_pairing_demo_pair(nullptr);
}

bool bt_pairing_demo_connect_saved(const BtPairingDemoCredentials& saved_credentials)
{
    M5.begin();

    if (!saved_credentials.valid || saved_credentials.version != kCredentialVersion)
    {
        ESP_LOGE(TAG, "saved credentials are missing or incompatible");
        return false;
    }

    if (!init_hfp())
    {
        return false;
    }

    if (!is_bonded(saved_credentials.remote_bda))
    {
        ESP_LOGW(TAG, "peer address is saved, but its link key is not in Bluedroid NVS");
    }

    log_bda("connecting saved peer", saved_credentials.remote_bda);
    return ok(esp_hf_client_connect(const_cast<uint8_t*>(saved_credentials.remote_bda)), "hfp connect saved");
}

bool bt_pairing_demo_connect_last()
{
    return bt_pairing_demo_connect_saved(g_last_credentials);
}

const BtPairingDemoCredentials* bt_pairing_demo_last_credentials()
{
    return g_last_credentials.valid ? &g_last_credentials : nullptr;
}

void bt_pairing_demo_shutdown_pairing()
{
    if (init_bluetooth())
    {
        close_pairing_window();
    }
}
