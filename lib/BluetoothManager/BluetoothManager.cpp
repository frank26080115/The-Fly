#include "BluetoothManager.h"

#include <Arduino.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "AudioManager.h"
#include "utilfuncs.h"

namespace BtManager
{
namespace
{

constexpr const char* TAG                 = "BtManager";
constexpr char        kLegacyPin[]        = "0000";
constexpr size_t      kLegacyPinMaxLength = sizeof(esp_bt_pin_code_t);
constexpr int         kHfpMaxVolume       = 15;

State                 g_state                  = State::Idle;
esp_bd_addr_t         g_target_mac             = {};
esp_bd_addr_t         g_connected_mac          = {};
PairedDevice          g_last_paired_device     = {};
bool                  g_has_connected_mac      = false;
bool                  g_has_last_paired_device = false;
bool                  g_bt_ready               = false;
bool                  g_hfp_ready              = false;
bool                  g_data_callback_ready    = false;
bool                  g_disconnect_requested   = false;
IncomingAudioCallback g_incoming_audio         = nullptr;
OutgoingAudioCallback g_outgoing_audio         = nullptr;
PairedCallback        g_paired_callback        = nullptr;
StateChangedCallback  g_state_changed_callback = nullptr;

char g_legacy_pin[kLegacyPinMaxLength + 1] = kLegacyPin;
// this is used during init and also when handling ESP_BT_GAP_PIN_REQ_EVT
// the user can supply it, or it defaults to kLegacyPin when none is specified

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

void close_pairing_or_waiting_window()
{
    ok(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE), "close bt discovery window");
}

Result result_from_esp(esp_err_t err, const char* what)
{
    return ok(err, what) ? Result::Ok : Result::EspError;
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

            if (g_paired_callback)
            {
                g_paired_callback(g_last_paired_device);
            }

            if (g_state == State::Pairing)
            {
                disconnect();
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
    if (!param)
    {
        return;
    }

    switch (event)
    {
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
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
            close_pairing_or_waiting_window();
            set_state(State::Connected);
            ok(esp_hf_client_connect_audio(g_connected_mac), "hfp audio connect");
            break;

        case ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTING:
            set_state(g_disconnect_requested ? State::Idle : State::Reconnecting);
            break;

        case ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED:
        default:
            g_has_connected_mac = false;
            if (g_disconnect_requested || g_state == State::Pairing)
            {
                g_disconnect_requested = false;
                set_state(State::Idle);
            }
            else if (g_state == State::Connected || g_state == State::Connecting || g_state == State::Reconnecting)
            {
                set_state(State::Reconnecting);
            }
            break;
        }
        break;

    case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED)
        {
            AudioManager::setHfpAudioFormat(AudioManager::HfpCodec::Cvsd, 8000);
            ESP_LOGI(TAG, "HFP CVSD/narrowband audio connected");
        }
        else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC)
        {
            AudioManager::setHfpAudioFormat(AudioManager::HfpCodec::Msbc, AudioManager::kSampleRateHz);
            ESP_LOGI(TAG, "HFP mSBC/wideband audio connected");
        }
        else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED && g_state == State::Connected && g_has_connected_mac)
        {
            AudioManager::setHfpAudioFormat(AudioManager::HfpCodec::Cvsd, 8000);
            ok(esp_hf_client_connect_audio(g_connected_mac), "hfp audio reconnect");
        }
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
        break;
    }
}

bool init_bluetooth(const char* device_name, const char* pin_code)
{
    // already performed, do not re-perform
    if (g_bt_ready)
    {
        ESP_LOGW(TAG, "init_bluetooth called after Bluetooth was already initialized");
        return true;
    }

    set_legacy_pin(pin_code); // copies into memory

    ok(esp_bt_controller_mem_release(ESP_BT_MODE_BLE), "release ble memory");

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (!ok(esp_bt_controller_init(&bt_cfg), "bt controller init") || !ok(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT), "bt controller enable") || !ok(esp_bluedroid_init(), "bluedroid init") || !ok(esp_bluedroid_enable(), "bluedroid enable"))
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

    ok(esp_bt_sleep_disable(), "bt sleep disable");
    ok(esp_bt_gap_register_callback(gap_event), "gap callback");
    ok(esp_bt_gap_set_device_name(device_name ? device_name : formatted_device_name), "bt name");
    ok(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap)), "ssp iocap");
    ok(esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, pin_length, pin), "legacy pin");
    ok(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE), "initial scan mode");
    ok(esp_bredr_sco_datapath_set(ESP_SCO_DATA_PATH_HCI), "sco hci path");

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

void setPairedCallback(PairedCallback callback)
{
    g_paired_callback = callback;
}

void setStateChangedCallback(StateChangedCallback callback)
{
    g_state_changed_callback = callback;
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

    if (g_state != State::Idle && g_state != State::Reconnecting && g_state != State::WaitingForIncomingConnection)
    {
        return Result::Busy;
    }

    close_pairing_or_waiting_window();
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

    if (g_state == State::Connected || g_state == State::Connecting)
    {
        return Result::Busy;
    }

    g_disconnect_requested   = false;
    g_has_last_paired_device = false;
    set_state(State::Pairing);
    return result_from_esp(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE), "open pairing window");
}

Result startWaitingForIncomingConnection()
{
    if (!init_hfp(nullptr, nullptr))
    {
        return Result::InitFailed;
    }

    if (g_state == State::Connected || g_state == State::Connecting || g_state == State::Pairing)
    {
        return Result::Busy;
    }

    g_disconnect_requested = false;
    set_state(State::WaitingForIncomingConnection);
    return result_from_esp(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE), "open incoming connection window");
}

Result disconnect()
{
    if (!g_bt_ready)
    {
        set_state(State::Idle);
        return Result::Ok;
    }

    g_disconnect_requested = true;
    close_pairing_or_waiting_window();

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

Result pickupPhone()
{
    if (!init_hfp(nullptr, nullptr))
    {
        return Result::InitFailed;
    }

    if (g_state != State::Connected)
    {
        return Result::Busy;
    }

    return result_from_esp(esp_hf_client_answer_call(), "hfp answer call");
}

void notifyOutgoingAudioReady()
{
    if (g_state == State::Connected)
    {
        esp_hf_client_outgoing_data_ready();
    }
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
    case State::Reconnecting:
        return "Reconnecting";
    case State::Pairing:
        return "Pairing";
    case State::WaitingForIncomingConnection:
        return "Waiting for Incoming Connection";
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
