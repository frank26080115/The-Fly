#include "BluetoothManager.h"

#include <Arduino.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp32-hal-bt.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "AudioManager.h"
#include "BtHostList.h"
#include "CallManager.h"
#include "utilfuncs.h"

namespace BtManager
{
namespace
{

constexpr const char* TAG                 = "BtManager";
constexpr char        kLegacyPin[]        = "0000";
constexpr size_t      kLegacyPinMaxLength = sizeof(esp_bt_pin_code_t);
constexpr size_t      kGeneratedPinLength = 4;
constexpr int         kHfpMaxVolume       = 15;
constexpr uint32_t    kAudioConnectRetryMs = 1000;
constexpr uint32_t    kAudioConnectTimeoutMs = 3000;
constexpr uint32_t    kAudioConnectTaskStack = 4096;
constexpr UBaseType_t kAudioConnectTaskPriority = 1;
constexpr uint32_t    kShutdownDisconnectWaitMs = 1500;

State                 g_state                  = State::Idle;
esp_bd_addr_t         g_target_mac             = {};
esp_bd_addr_t         g_connected_mac          = {};
PairedDevice          g_last_paired_device     = {};
BtHostList            g_host_list;
bool                  g_has_connected_mac      = false;
bool                  g_has_last_paired_device = false;
bool                  g_bt_ready               = false;
bool                  g_hfp_ready              = false;
bool                  g_data_callback_ready    = false;
bool                  g_disconnect_requested   = false;
volatile bool         g_hfp_audio_connecting   = false;
volatile bool         g_hfp_audio_connected    = false;
volatile bool         g_hfp_call_active        = false;
volatile bool         g_hfp_call_setup_active  = false;
volatile bool         g_hfp_answer_requested   = false;
uint32_t              g_hfp_audio_connect_started_ms = 0;
TaskHandle_t          g_audio_connect_task     = nullptr;
IncomingAudioCallback g_incoming_audio         = nullptr;
OutgoingAudioCallback g_outgoing_audio         = nullptr;
PairedCallback        g_paired_callback        = nullptr;
StateChangedCallback  g_state_changed_callback = nullptr;

char g_legacy_pin[kLegacyPinMaxLength + 1] = { '0', '0', '0', '0', '\0' };
// this is used during init and also when handling ESP_BT_GAP_PIN_REQ_EVT
// the user can supply it, or it defaults to kLegacyPin when none is specified
char g_generated_legacy_pin[kGeneratedPinLength + 1] = {};
bool g_has_generated_legacy_pin                      = false;

// very simply sets the state of the state machine, with an optional callback
void set_state(State next)
{
    if (g_state == next)
    {
        return;
    }

    g_state = next;
    if (g_state_changed_callback)
    {
        g_state_changed_callback(g_state);
    }
}

bool hfp_control_connected()
{
    return g_state == State::Connected || g_state == State::AudioAvailable;
}

void close_pairing_window()
{
    ok(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE), "close bt discovery window");
}

void make_bluetooth_non_connectable()
{
    ok(esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE), "close bt radio window");
}

Result result_from_esp(esp_err_t err, const char* what)
{
    return strict_ok(err, what) ? Result::Ok : Result::EspError;
}

esp_bt_cod_t handsfree_cod()
{
    esp_bt_cod_t cod = {};
    cod.major        = ESP_BT_COD_MAJOR_DEV_AV;
    cod.minor        = 0x01;
    cod.service      = ESP_BT_COD_SRVC_AUDIO | ESP_BT_COD_SRVC_TELEPHONY;
    return cod;
}

const char* controller_status_name(esp_bt_controller_status_t status)
{
    switch (status)
    {
    case ESP_BT_CONTROLLER_STATUS_IDLE:
        return "IDLE";
    case ESP_BT_CONTROLLER_STATUS_INITED:
        return "INITED";
    case ESP_BT_CONTROLLER_STATUS_ENABLED:
        return "ENABLED";
    default:
        return "UNKNOWN";
    }
}

const char* bluedroid_status_name(esp_bluedroid_status_t status)
{
    switch (status)
    {
    case ESP_BLUEDROID_STATUS_UNINITIALIZED:
        return "UNINITIALIZED";
    case ESP_BLUEDROID_STATUS_INITIALIZED:
        return "INITIALIZED";
    case ESP_BLUEDROID_STATUS_ENABLED:
        return "ENABLED";
    default:
        return "UNKNOWN";
    }
}

const char* hfp_event_name(esp_hf_client_cb_event_t event)
{
    switch (event)
    {
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        return "ESP_HF_CLIENT_CONNECTION_STATE_EVT";
    case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        return "ESP_HF_CLIENT_AUDIO_STATE_EVT";
    case ESP_HF_CLIENT_BVRA_EVT:
        return "ESP_HF_CLIENT_BVRA_EVT";
    case ESP_HF_CLIENT_CIND_CALL_EVT:
        return "ESP_HF_CLIENT_CIND_CALL_EVT";
    case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT:
        return "ESP_HF_CLIENT_CIND_CALL_SETUP_EVT";
    case ESP_HF_CLIENT_CIND_CALL_HELD_EVT:
        return "ESP_HF_CLIENT_CIND_CALL_HELD_EVT";
    case ESP_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT:
        return "ESP_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT";
    case ESP_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT:
        return "ESP_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT";
    case ESP_HF_CLIENT_CIND_ROAMING_STATUS_EVT:
        return "ESP_HF_CLIENT_CIND_ROAMING_STATUS_EVT";
    case ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT:
        return "ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT";
    case ESP_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT:
        return "ESP_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT";
    case ESP_HF_CLIENT_BTRH_EVT:
        return "ESP_HF_CLIENT_BTRH_EVT";
    case ESP_HF_CLIENT_CLIP_EVT:
        return "ESP_HF_CLIENT_CLIP_EVT";
    case ESP_HF_CLIENT_CCWA_EVT:
        return "ESP_HF_CLIENT_CCWA_EVT";
    case ESP_HF_CLIENT_CLCC_EVT:
        return "ESP_HF_CLIENT_CLCC_EVT";
    case ESP_HF_CLIENT_VOLUME_CONTROL_EVT:
        return "ESP_HF_CLIENT_VOLUME_CONTROL_EVT";
    case ESP_HF_CLIENT_AT_RESPONSE_EVT:
        return "ESP_HF_CLIENT_AT_RESPONSE_EVT";
    case ESP_HF_CLIENT_CNUM_EVT:
        return "ESP_HF_CLIENT_CNUM_EVT";
    case ESP_HF_CLIENT_BSIR_EVT:
        return "ESP_HF_CLIENT_BSIR_EVT";
    case ESP_HF_CLIENT_BINP_EVT:
        return "ESP_HF_CLIENT_BINP_EVT";
    case ESP_HF_CLIENT_RING_IND_EVT:
        return "ESP_HF_CLIENT_RING_IND_EVT";
    case ESP_HF_CLIENT_PKT_STAT_NUMS_GET_EVT:
        return "ESP_HF_CLIENT_PKT_STAT_NUMS_GET_EVT";
    default:
        return "ESP_HF_CLIENT_UNKNOWN_EVT";
    }
}

const char* hfp_connection_state_name(esp_hf_client_connection_state_t state)
{
    switch (state)
    {
    case ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED:
        return "DISCONNECTED";
    case ESP_HF_CLIENT_CONNECTION_STATE_CONNECTING:
        return "CONNECTING";
    case ESP_HF_CLIENT_CONNECTION_STATE_CONNECTED:
        return "CONNECTED";
    case ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED:
        return "SLC_CONNECTED";
    case ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTING:
        return "DISCONNECTING";
    default:
        return "UNKNOWN";
    }
}

const char* hfp_audio_state_name(esp_hf_client_audio_state_t state)
{
    switch (state)
    {
    case ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED:
        return "DISCONNECTED";
    case ESP_HF_CLIENT_AUDIO_STATE_CONNECTING:
        return "CONNECTING";
    case ESP_HF_CLIENT_AUDIO_STATE_CONNECTED:
        return "CONNECTED";
    case ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC:
        return "CONNECTED_MSBC";
    default:
        return "UNKNOWN";
    }
}

bool ensure_controller_enabled()
{
    esp_bt_controller_status_t status = esp_bt_controller_get_status();
    ESP_LOGI(TAG, "bt controller status: %s", controller_status_name(status));

    if (btStarted())
    {
        ESP_LOGI(TAG, "Arduino btStarted: yes");
        return true;
    }

    ESP_LOGI(TAG, "Arduino btStart");
    if (!btStart())
    {
        status = esp_bt_controller_get_status();
        ESP_LOGE(TAG, "Arduino btStart failed, controller status: %s", controller_status_name(status));
        return false;
    }

    status = esp_bt_controller_get_status();
    ESP_LOGI(TAG, "bt controller status after Arduino btStart: %s", controller_status_name(status));
    return status == ESP_BT_CONTROLLER_STATUS_ENABLED;
}

bool ensure_bluedroid_enabled()
{
    esp_bluedroid_status_t status = esp_bluedroid_get_status();
    ESP_LOGI(TAG, "bluedroid status: %s", bluedroid_status_name(status));

    if (status == ESP_BLUEDROID_STATUS_UNINITIALIZED)
    {
        if (!strict_ok(esp_bluedroid_init(), "bluedroid init"))
        {
            return false;
        }
        status = esp_bluedroid_get_status();
        ESP_LOGI(TAG, "bluedroid status after init: %s", bluedroid_status_name(status));
    }

    if (status == ESP_BLUEDROID_STATUS_INITIALIZED)
    {
        if (!strict_ok(esp_bluedroid_enable(), "bluedroid enable"))
        {
            return false;
        }
        status = esp_bluedroid_get_status();
        ESP_LOGI(TAG, "bluedroid status after enable: %s", bluedroid_status_name(status));
    }

    return status == ESP_BLUEDROID_STATUS_ENABLED;
}

bool bonded_mac_matches(const esp_bd_addr_t mac)
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
            if (bda_equal(mac, bonded[i]))
            {
                found = true;
                break;
            }
        }
    }

    free(bonded);
    return found;
}

uint32_t outgoing_audio_adapter(uint8_t* buf, uint32_t len)
{
    return g_outgoing_audio ? g_outgoing_audio(buf, len) : 0;
}

size_t legacy_pin_length()
{
    return strnlen(g_legacy_pin, kLegacyPinMaxLength);
}

void set_legacy_pin(const char* pin)
{
    strlcpy(g_legacy_pin, pin ? pin : kLegacyPin, sizeof(g_legacy_pin));
}

void format_pin_from_mac(const esp_bd_addr_t mac, char* pin, size_t pin_size)
{
    const uint16_t suffix = (static_cast<uint16_t>(mac[ESP_BD_ADDR_LEN - 2]) << 8) | mac[ESP_BD_ADDR_LEN - 1];
    uint16_t       value  = suffix % 10000;
    if (value == 0)
    {
        // avoid a all 0 result
        value = 1;
    }
    snprintf(pin, pin_size, "%04u", static_cast<unsigned>(value));
}

void format_device_name(char* name, size_t name_size)
{
    esp_bd_addr_t mac = {};
    if (!ok(esp_read_mac(mac, ESP_MAC_BT), "read bt mac"))
    {
        snprintf(name, name_size, "The Fly 0000");
        return;
    }

    snprintf(name, name_size, "The Fly %02X%02X", mac[ESP_BD_ADDR_LEN - 2], mac[ESP_BD_ADDR_LEN - 1]);
}

void incoming_audio_adapter(const uint8_t* buf, uint32_t len)
{
    if (g_incoming_audio)
    {
        g_incoming_audio(buf, len);
    }
}

bool register_data_callbacks_if_ready()
{
    if (g_data_callback_ready)
    {
        return true;
    }

    if (!g_incoming_audio || !g_outgoing_audio)
    {
        return true;
    }

    if (!ok(esp_hf_client_register_data_callback(incoming_audio_adapter, outgoing_audio_adapter), "hfp data callback"))
    {
        return false;
    }

    g_data_callback_ready = true;
    return true;
}

bool hfp_audio_connect_timed_out()
{
    return g_hfp_audio_connecting && (millis() - g_hfp_audio_connect_started_ms) >= kAudioConnectTimeoutMs;
}

esp_err_t connect_hfp_audio_once(const char* reason)
{
    if (!g_has_connected_mac)
    {
        ESP_LOGW(TAG, "hfp audio connect skipped (%s): no connected MAC", reason);
        return ESP_ERR_INVALID_STATE;
    }

    if (g_hfp_audio_connected)
    {
        ESP_LOGI(TAG, "hfp audio connect skipped (%s): already connected", reason);
        return ESP_OK;
    }

    if (g_hfp_audio_connecting)
    {
        if (!hfp_audio_connect_timed_out())
        {
            ESP_LOGI(TAG, "hfp audio connect skipped (%s): already connecting", reason);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "hfp audio connect timed out while %s, retrying", reason);
        g_hfp_audio_connecting = false;
    }

    const esp_err_t err = esp_hf_client_connect_audio(g_connected_mac);
    ESP_LOGI(TAG, "esp_hf_client_connect_audio (%s) returned: %s", reason, esp_err_to_name(err));
    if (err == ESP_OK)
    {
        g_hfp_audio_connecting = true;
        g_hfp_audio_connect_started_ms = millis();
    }
    return err;
}

void answer_incoming_call_once(const char* reason)
{
    if (g_hfp_answer_requested || g_hfp_call_active)
    {
        return;
    }

    const esp_err_t err = esp_hf_client_answer_call();
    ESP_LOGI(TAG, "esp_hf_client_answer_call (%s) returned: %s", reason, esp_err_to_name(err));
    if (err == ESP_OK)
    {
        g_hfp_answer_requested = true;
    }
}

void audio_connect_retry_task(void*)
{
    ESP_LOGI(TAG, "hfp audio connect retry task started");

    while (hfp_control_connected() && g_has_connected_mac && !g_hfp_audio_connected && !g_disconnect_requested && g_hfp_call_active)
    {
        if (!g_hfp_audio_connecting || hfp_audio_connect_timed_out())
        {
            connect_hfp_audio_once("retry task");
        }
        vTaskDelay(pdMS_TO_TICKS(kAudioConnectRetryMs));
    }

    ESP_LOGI(TAG, "hfp audio connect retry task stopped");
    g_audio_connect_task = nullptr;
    vTaskDelete(nullptr);
}

void start_audio_connect_retries()
{
    if (g_audio_connect_task || g_hfp_audio_connected)
    {
        return;
    }

    const BaseType_t created = xTaskCreate(audio_connect_retry_task,
                                           "hfp_audio_retry",
                                           kAudioConnectTaskStack,
                                           nullptr,
                                           kAudioConnectTaskPriority,
                                           &g_audio_connect_task);
    if (created != pdPASS)
    {
        g_audio_connect_task = nullptr;
        ESP_LOGE(TAG, "failed to create hfp audio connect retry task");
    }
}

const char* connected_host_name(const esp_bd_addr_t mac)
{
    for (size_t i = 0; i < g_host_list.size(); ++i)
    {
        const bt_host_item_t* item = g_host_list.get(i);
        if (item && item->name && item->name[0] != '\0' && bda_equal(item->bdaddr, mac))
        {
            return item->name;
        }
    }

    if (g_has_last_paired_device && g_last_paired_device.name[0] != '\0' && bda_equal(g_last_paired_device.mac, mac))
    {
        return g_last_paired_device.name;
    }

    return nullptr;
}

void query_call_metadata(const char* reason)
{
    ESP_LOGI(TAG, "querying HFP call metadata: %s", reason);
    ok(esp_hf_client_query_current_calls(), "hfp query current calls");
}

void gap_event(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param)
{
    switch (event)
    {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (!param)
        {
            return;
        }

        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
        {
            g_last_paired_device = {};
            copy_bda(g_last_paired_device.mac, param->auth_cmpl.bda);
            strlcpy(g_last_paired_device.name, reinterpret_cast<const char*>(param->auth_cmpl.device_name), sizeof(g_last_paired_device.name));
            g_has_last_paired_device = true;
            log_bda("paired with", g_last_paired_device.mac);
            g_host_list.insert(g_last_paired_device.name, g_last_paired_device.mac);

            if (g_paired_callback)
            {
                g_paired_callback(g_last_paired_device);
            }

            if (g_state == State::Pairing)
            {
                close_pairing_window();
                set_state(State::Idle);
            }
        }
        else
        {
            ESP_LOGW(TAG, "pairing failed, status=%d", param->auth_cmpl.stat);
        }
        break;

    case ESP_BT_GAP_PIN_REQ_EVT:
    {
        if (!param)
        {
            return;
        }

        esp_bt_pin_code_t pin        = {};
        const size_t      pin_length = legacy_pin_length();
        memcpy(pin, g_legacy_pin, pin_length);
        ok(esp_bt_gap_pin_reply(param->pin_req.bda, true, pin_length, pin), "legacy pin reply");
        break;
    }

    case ESP_BT_GAP_CFM_REQ_EVT:
        if (param)
        {
            ESP_LOGI(TAG, "confirming SSP numeric comparison: %" PRIu32, param->cfm_req.num_val);
            ok(esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true), "ssp confirm reply");
        }
        break;

    case ESP_BT_GAP_KEY_NOTIF_EVT:
        if (param)
        {
            ESP_LOGI(TAG, "SSP passkey: %" PRIu32, param->key_notif.passkey);
        }
        break;

    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGW(TAG, "remote requested passkey entry; this device has no keyboard");
        break;

    default:
        break;
    }
}

void hfp_event(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t* param)
{
    ESP_LOGI(TAG, "hfp_event: %s (%d)", hfp_event_name(event), static_cast<int>(event));

    if (!param)
    {
        ESP_LOGW(TAG, "hfp_event %s had null param", hfp_event_name(event));
        return;
    }

    switch (event)
    {
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        ESP_LOGI(TAG,
                 "hfp connection state: %s (%d), peer_feat=0x%08" PRIx32 ", chld_feat=0x%08" PRIx32,
                 hfp_connection_state_name(param->conn_stat.state),
                 static_cast<int>(param->conn_stat.state),
                 param->conn_stat.peer_feat,
                 param->conn_stat.chld_feat);
        switch (param->conn_stat.state)
        {
        case ESP_HF_CLIENT_CONNECTION_STATE_CONNECTING:
            set_state(g_disconnect_requested ? State::Idle : State::Connecting);
            break;

        case ESP_HF_CLIENT_CONNECTION_STATE_CONNECTED:
            copy_bda(g_connected_mac, param->conn_stat.remote_bda);
            g_has_connected_mac = true;
            break;

        case ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED:
            copy_bda(g_connected_mac, param->conn_stat.remote_bda);
            g_has_connected_mac = true;
            g_hfp_audio_connecting = false;
            g_hfp_audio_connected = false;
            g_hfp_call_active = false;
            g_hfp_call_setup_active = false;
            g_hfp_answer_requested = false;
            g_hfp_audio_connect_started_ms = 0;
            CallManager::onBluetoothConnectionEstablished(connected_host_name(g_connected_mac));
            ok(esp_hf_client_retrieve_subscriber_info(), "hfp retrieve subscriber info");
            close_pairing_window();
            set_state(State::Connected);
            break;

        case ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTING:
            set_state(g_disconnect_requested ? State::Idle : State::Reconnecting);
            break;

        case ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED:
        default:
            g_has_connected_mac = false;
            g_hfp_audio_connecting = false;
            g_hfp_audio_connected = false;
            g_hfp_call_active = false;
            g_hfp_call_setup_active = false;
            g_hfp_answer_requested = false;
            g_hfp_audio_connect_started_ms = 0;
            CallManager::onBluetoothDisconnected();
            if (g_disconnect_requested || g_state == State::Pairing)
            {
                g_disconnect_requested = false;
                set_state(State::Idle);
            }
            else if (hfp_control_connected() || g_state == State::Connecting || g_state == State::Reconnecting)
            {
                set_state(State::Reconnecting);
            }
            break;
        }
        break;

    case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        ESP_LOGI(TAG,
                 "hfp audio state: %s (%d), sync_conn_handle=%u",
                 hfp_audio_state_name(param->audio_stat.state),
                 static_cast<int>(param->audio_stat.state),
                 static_cast<unsigned>(param->audio_stat.sync_conn_handle));
        if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTING)
        {
            g_hfp_audio_connecting = true;
            if (g_hfp_audio_connect_started_ms == 0)
            {
                g_hfp_audio_connect_started_ms = millis();
            }
        }
        if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED)
        {
            g_hfp_audio_connecting = false;
            g_hfp_audio_connected = false;
            g_hfp_audio_connect_started_ms = 0;
            AudioManager::setHfpAudioFormat(AudioManager::HfpCodec::Cvsd, 8000);
            g_hfp_audio_connected = true;
            CallManager::setScoAudioConnected(true);
            set_state(State::AudioAvailable);
            ESP_LOGI(TAG, "HFP CVSD/narrowband audio connected");
        }
        else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC)
        {
            g_hfp_audio_connecting = false;
            g_hfp_audio_connected = false;
            g_hfp_audio_connect_started_ms = 0;
            AudioManager::setHfpAudioFormat(AudioManager::HfpCodec::Msbc, AudioManager::kSampleRateHz);
            g_hfp_audio_connected = true;
            CallManager::setScoAudioConnected(true);
            set_state(State::AudioAvailable);
            ESP_LOGI(TAG, "HFP mSBC/wideband audio connected");
        }
        else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED && hfp_control_connected() && g_has_connected_mac)
        {
            g_hfp_audio_connecting = false;
            g_hfp_audio_connected = false;
            g_hfp_audio_connect_started_ms = 0;
            AudioManager::setHfpAudioFormat(AudioManager::HfpCodec::Cvsd, 8000);
            CallManager::setScoAudioConnected(false);
            set_state(State::Connected);
        }
        break;

    case ESP_HF_CLIENT_CIND_CALL_EVT:
        ESP_LOGI(TAG, "hfp call status: %d", static_cast<int>(param->call.status));
        CallManager::setCallStatus(static_cast<int>(param->call.status));
        g_hfp_call_active = param->call.status != 0;
        if (!g_hfp_call_active)
        {
            g_hfp_call_setup_active = false;
            g_hfp_answer_requested = false;
            CallManager::setCallSetupStatus(0);
        }
        if (param->call.status && !g_hfp_audio_connected)
        {
            query_call_metadata("call active");
            connect_hfp_audio_once("call active");
            start_audio_connect_retries();
        }
        break;

    case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT:
        ESP_LOGI(TAG, "hfp call setup status: %d", static_cast<int>(param->call_setup.status));
        CallManager::setCallSetupStatus(static_cast<int>(param->call_setup.status));
        g_hfp_call_setup_active = param->call_setup.status != 0;
        if (param->call_setup.status == ESP_HF_CALL_SETUP_STATUS_INCOMING)
        {
            query_call_metadata("incoming call setup");
            answer_incoming_call_once("incoming call setup");
        }
        else if (param->call_setup.status != ESP_HF_CALL_SETUP_STATUS_IDLE)
        {
            query_call_metadata("call setup");
        }
        break;

    case ESP_HF_CLIENT_CIND_CALL_HELD_EVT:
        ESP_LOGI(TAG, "hfp call held status: %d", static_cast<int>(param->call_held.status));
        CallManager::setCallHeldStatus(static_cast<int>(param->call_held.status));
        break;

    case ESP_HF_CLIENT_AT_RESPONSE_EVT:
        ESP_LOGI(TAG, "hfp AT response: code=%d cme=%d", static_cast<int>(param->at_response.code), static_cast<int>(param->at_response.cme));
        break;

    case ESP_HF_CLIENT_CLIP_EVT:
        ESP_LOGI(TAG, "hfp caller id: %s", param->clip.number ? param->clip.number : "(null)");
        CallManager::addCallerInfo(param->clip.number);
        break;

    case ESP_HF_CLIENT_CCWA_EVT:
        ESP_LOGI(TAG, "hfp call waiting: %s", param->ccwa.number ? param->ccwa.number : "(null)");
        CallManager::addCallerInfo(param->ccwa.number);
        break;

    case ESP_HF_CLIENT_CLCC_EVT:
        ESP_LOGI(TAG,
                 "hfp current call: idx=%d dir=%d status=%d mpty=%d number=%s",
                 param->clcc.idx,
                 static_cast<int>(param->clcc.dir),
                 static_cast<int>(param->clcc.status),
                 static_cast<int>(param->clcc.mpty),
                 param->clcc.number ? param->clcc.number : "(null)");
        CallManager::addCallerInfo(param->clcc.number);
        break;

    case ESP_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT:
        ESP_LOGI(TAG, "hfp service availability: %d", static_cast<int>(param->service_availability.status));
        break;

    case ESP_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT:
        ESP_LOGI(TAG, "hfp signal strength: %d", param->signal_strength.value);
        break;

    case ESP_HF_CLIENT_CIND_ROAMING_STATUS_EVT:
        ESP_LOGI(TAG, "hfp roaming status: %d", static_cast<int>(param->roaming.status));
        break;

    case ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT:
        ESP_LOGI(TAG, "hfp battery level: %d", param->battery_level.value);
        break;

    case ESP_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT:
        ESP_LOGI(TAG, "hfp operator: %s", param->cops.name ? param->cops.name : "(null)");
        break;

    case ESP_HF_CLIENT_BVRA_EVT:
        ESP_LOGI(TAG, "hfp voice recognition state: %d", static_cast<int>(param->bvra.value));
        break;

    case ESP_HF_CLIENT_BTRH_EVT:
        ESP_LOGI(TAG, "hfp response and hold status: %d", static_cast<int>(param->btrh.status));
        break;

    case ESP_HF_CLIENT_CNUM_EVT:
        ESP_LOGI(TAG, "hfp subscriber number: %s type=%d", param->cnum.number ? param->cnum.number : "(null)", static_cast<int>(param->cnum.type));
        CallManager::addCallerInfo(param->cnum.number);
        break;

    case ESP_HF_CLIENT_BSIR_EVT:
        ESP_LOGI(TAG, "hfp in-band ringtone state: %d", static_cast<int>(param->bsir.state));
        break;

    case ESP_HF_CLIENT_BINP_EVT:
        ESP_LOGI(TAG, "hfp voice tag number: %s", param->binp.number ? param->binp.number : "(null)");
        CallManager::addCallerInfo(param->binp.number);
        break;

    case ESP_HF_CLIENT_RING_IND_EVT:
        ESP_LOGI(TAG, "hfp ring indication");
        g_hfp_call_setup_active = true;
        CallManager::setCallSetupStatus(ESP_HF_CALL_SETUP_STATUS_INCOMING);
        query_call_metadata("ring indication");
        answer_incoming_call_once("ring indication");
        break;

    case ESP_HF_CLIENT_PKT_STAT_NUMS_GET_EVT:
        ESP_LOGI(TAG,
                 "hfp packet stats: rx_total=%" PRIu32 " rx_correct=%" PRIu32 " rx_err=%" PRIu32 " rx_none=%" PRIu32 " rx_lost=%" PRIu32 " tx_total=%" PRIu32 " tx_discarded=%" PRIu32,
                 param->pkt_nums.rx_total,
                 param->pkt_nums.rx_correct,
                 param->pkt_nums.rx_err,
                 param->pkt_nums.rx_none,
                 param->pkt_nums.rx_lost,
                 param->pkt_nums.tx_total,
                 param->pkt_nums.tx_discarded);
        break;

    case ESP_HF_CLIENT_VOLUME_CONTROL_EVT:
        if (param->volume_control.type == ESP_HF_VOLUME_CONTROL_TARGET_SPK)
        {
            const int  clamped_hfp_volume = constrain(param->volume_control.volume, 0, kHfpMaxVolume);
            const auto audio_volume       = static_cast<uint8_t>(clamped_hfp_volume * 2);
            AudioManager::setVolume(audio_volume);
            ESP_LOGI(TAG, "speaker volume set from HFP: %d -> %u", clamped_hfp_volume, audio_volume);
        }
        else if (param->volume_control.type == ESP_HF_VOLUME_CONTROL_TARGET_MIC)
        {
            ESP_LOGI(TAG, "HFP microphone gain request ignored: %d", param->volume_control.volume);
        }
        break;

    default:
        ESP_LOGI(TAG, "hfp event has no detailed logger yet: %s (%d)", hfp_event_name(event), static_cast<int>(event));
        break;
    }
}

bool init_bluetooth(const char* device_name, const char* pin_code)
{
    // already performed, do not re-perform
    if (g_bt_ready)
    {
        return true;
    }

    set_legacy_pin(pin_code); // copies into memory

    if (!ensure_controller_enabled() || !ensure_bluedroid_enabled())
    {
        return false;
    }

    esp_bt_io_cap_t   iocap                                       = ESP_BT_IO_CAP_NONE;
    esp_bt_pin_code_t pin                                         = {};
    char              formatted_device_name[kDeviceNameMaxLength] = {};
    const size_t      pin_length                                  = legacy_pin_length();
    memcpy(pin, g_legacy_pin, pin_length);
    if (!device_name)
    {
        format_device_name(formatted_device_name, sizeof(formatted_device_name));
    }

    if (   !strict_ok(esp_bt_sleep_disable(), "bt sleep disable")
        || !strict_ok(esp_bt_gap_register_callback(gap_event), "gap callback")
        || !strict_ok(esp_bt_gap_set_device_name(device_name ? device_name : formatted_device_name), "bt name")
        || !strict_ok(esp_bt_gap_set_cod(handsfree_cod(), ESP_BT_INIT_COD), "bt class of device")
        || !strict_ok(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap)), "ssp iocap")
        || !strict_ok(esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, pin_length, pin), "legacy pin")
        || !strict_ok(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE), "initial scan mode")
        || !strict_ok(esp_bredr_sco_datapath_set(ESP_SCO_DATA_PATH_HCI), "sco hci path")
        )
    {
        return false;
    }

    g_bt_ready = true;
    return true;
}

bool init_hfp(const char* device_name, const char* pin_code)
{
    if (!init_bluetooth(device_name, pin_code))
    {
        return false;
    }

    if (!g_hfp_ready)
    {
        if (!ok(esp_hf_client_register_callback(hfp_event), "hfp callback") || !register_data_callbacks_if_ready() || !ok(esp_hf_client_init(), "hfp init"))
        {
            return false;
        }
        g_hfp_ready = true;
    }

    return register_data_callbacks_if_ready();
}

} // namespace

bool init(const char* deviceName, IncomingAudioCallback incomingAudio, OutgoingAudioCallback outgoingAudio, const char* pin)
{
    g_incoming_audio = incomingAudio;
    g_outgoing_audio = outgoingAudio;
    return init_hfp(deviceName, pin);
}

void setAudioCallbacks(IncomingAudioCallback incomingAudio, OutgoingAudioCallback outgoingAudio)
{
    g_incoming_audio = incomingAudio;
    g_outgoing_audio = outgoingAudio;
    register_data_callbacks_if_ready();
}

bool generateLegacyPinFromMac()
{
    esp_bd_addr_t mac = {};
    const bool    read_mac = ok(esp_read_mac(mac, ESP_MAC_BT), "read bt mac for pin");
    if (read_mac)
    {
        format_pin_from_mac(mac, g_generated_legacy_pin, sizeof(g_generated_legacy_pin));
    }
    else
    {
        strlcpy(g_generated_legacy_pin, "0001", sizeof(g_generated_legacy_pin));
    }

    g_has_generated_legacy_pin = true;
    set_legacy_pin(g_generated_legacy_pin);
    return read_mac;
}

const char* generatedLegacyPin()
{
    if (!g_has_generated_legacy_pin)
    {
        generateLegacyPinFromMac();
    }
    return g_generated_legacy_pin;
}

void setPairedCallback(PairedCallback callback)
{
    g_paired_callback = callback;
}

void setStateChangedCallback(StateChangedCallback callback)
{
    g_state_changed_callback = callback;
}

BtHostList& hostList()
{
    return g_host_list;
}

Result connectToMac(const char* mac)
{
    esp_bd_addr_t parsed = {};
    if (!parse_mac(mac, parsed))
    {
        return Result::InvalidArgument;
    }

    return connectToMac(parsed);
}

Result connectToMac(const esp_bd_addr_t mac)
{
    if (!init_hfp(nullptr, nullptr))
    {
        return Result::InitFailed;
    }

    if (!bonded_mac_matches(mac))
    {
        return Result::NotBonded;
    }

    if (g_state != State::Idle && g_state != State::Reconnecting)
    {
        return Result::Busy;
    }

    close_pairing_window();
    copy_bda(g_target_mac, mac);
    g_disconnect_requested = false;
    set_state(State::Connecting);
    return result_from_esp(esp_hf_client_connect(g_target_mac), "hfp connect");
}

Result startPairing()
{
    if (!init_hfp(nullptr, nullptr))
    {
        return Result::InitFailed;
    }

    if (hfp_control_connected() || g_state == State::Connecting)
    {
        return Result::Busy;
    }

    g_disconnect_requested   = false;
    g_has_last_paired_device = false;
    set_state(State::Pairing);
    return result_from_esp(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE), "open pairing window");
}

Result disconnect()
{
    if (!g_bt_ready)
    {
        set_state(State::Idle);
        return Result::Ok;
    }

    g_disconnect_requested = true;
    close_pairing_window();

    if (g_has_connected_mac)
    {
        ok(esp_hf_client_disconnect_audio(g_connected_mac), "hfp audio disconnect");
        ok(esp_hf_client_disconnect(g_connected_mac), "hfp disconnect");
    }
    else
    {
        set_state(State::Idle);
        g_disconnect_requested = false;
    }

    return Result::Ok;
}

Result shutdown()
{
    if (!g_bt_ready && !btStarted() && esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED)
    {
        set_state(State::Idle);
        return Result::Ok;
    }

    g_disconnect_requested = true;
    if (g_bt_ready)
    {
        make_bluetooth_non_connectable();
    }

    if (g_has_connected_mac)
    {
        ok(esp_hf_client_disconnect_audio(g_connected_mac), "hfp audio disconnect");
        ok(esp_hf_client_disconnect(g_connected_mac), "hfp disconnect");

        const uint32_t started_ms = millis();
        while (g_has_connected_mac && static_cast<uint32_t>(millis() - started_ms) < kShutdownDisconnectWaitMs)
        {
            delay(10);
        }
    }

    bool ok_all = true;
    if (g_hfp_ready)
    {
        ok_all = ok(esp_hf_client_deinit(), "hfp deinit") && ok_all;
    }

    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED)
    {
        ok_all = ok(esp_bluedroid_disable(), "bluedroid disable") && ok_all;
    }
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED)
    {
        ok_all = ok(esp_bluedroid_deinit(), "bluedroid deinit") && ok_all;
    }
    if (btStarted())
    {
        ok_all = btStop() && ok_all;
    }

    g_has_connected_mac = false;
    g_hfp_audio_connecting = false;
    g_hfp_audio_connected = false;
    g_hfp_call_active = false;
    g_hfp_call_setup_active = false;
    g_hfp_answer_requested = false;
    g_hfp_audio_connect_started_ms = 0;
    g_audio_connect_task = nullptr;
    g_hfp_ready = false;
    g_bt_ready = false;
    g_data_callback_ready = false;
    g_disconnect_requested = false;
    set_state(State::Idle);
    return ok_all ? Result::Ok : Result::EspError;
}

Result pickupPhone()
{
    if (!init_hfp(nullptr, nullptr))
    {
        return Result::InitFailed;
    }

    if (!hfp_control_connected())
    {
        return Result::Busy;
    }

    return result_from_esp(esp_hf_client_answer_call(), "hfp answer call");
}

void notifyOutgoingAudioReady()
{
    if (canNotifyOutgoingAudioReady())
    {
        esp_hf_client_outgoing_data_ready();
    }
}

bool canNotifyOutgoingAudioReady()
{
    return hfp_control_connected() && g_hfp_audio_connected && g_data_callback_ready;
}

State state()
{
    return g_state;
}

const esp_bd_addr_t& connectedMac()
{
    return g_connected_mac;
}

const PairedDevice& lastPairedDevice()
{
    return g_last_paired_device;
}

bool hasLastPairedDevice()
{
    return g_has_last_paired_device;
}

bool isBonded(const esp_bd_addr_t mac)
{
    return g_bt_ready && bonded_mac_matches(mac);
}

bool isBonded(const char* mac)
{
    esp_bd_addr_t parsed = {};
    return parse_mac(mac, parsed) && isBonded(parsed);
}

const char* stateName(State value)
{
    switch (value)
    {
    case State::Idle:
        return "Idle";
    case State::Connecting:
        return "Connecting";
    case State::Connected:
        return "Connected";
    case State::AudioAvailable:
        return "Audio Available";
    case State::Reconnecting:
        return "Reconnecting";
    case State::Pairing:
        return "Pairing";
    default:
        return "Unknown";
    }
}

const char* resultName(Result value)
{
    switch (value)
    {
    case Result::Ok:
        return "Ok";
    case Result::InvalidArgument:
        return "Invalid Argument";
    case Result::InitFailed:
        return "Init Failed";
    case Result::NotBonded:
        return "Not Bonded";
    case Result::Busy:
        return "Busy";
    case Result::EspError:
        return "ESP Error";
    default:
        return "Unknown";
    }
}

} // namespace BtManager
