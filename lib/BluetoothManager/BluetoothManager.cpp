#include "BluetoothManager.h"

#include <Arduino.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp32-hal-bt.h"
#include "dbg_log.h"
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

constexpr const char* TAG                              = "BtManager";
constexpr char        kLegacyPin[]                     = "0000";
constexpr size_t      kLegacyPinMaxLength              = sizeof(esp_bt_pin_code_t);
constexpr size_t      kGeneratedPinLength              = 4;
constexpr int         kHfpMaxVolume                    = 15;
constexpr uint32_t    kAudioConnectRetryMs             = 1000;
constexpr uint32_t    kAudioConnectTimeoutMs           = 3000;
constexpr uint32_t    kAudioConnectTaskStack           = 4096;
constexpr UBaseType_t kAudioConnectTaskPriority        = 1;
constexpr uint32_t    kHfpProfileInitTimeoutMs         = 2000;
constexpr uint32_t    kShutdownDisconnectWaitMs        = 1500;
constexpr uint32_t    kReconnectAttemptIntervalMs      = 2000;
constexpr uint32_t    kOutboundConnectScanModeSettleMs = 200;

State                 g_state                 = State::Idle;
esp_bd_addr_t         g_target_mac            = {};
esp_bd_addr_t         g_connected_mac         = {};
esp_bd_addr_t         g_reconnect_target_mac  = {};
PairedDevice          g_last_paired_device    = {};
PairedDevice          g_pending_paired_device = {};
BtHostList            g_host_list;
bool                  g_has_connected_mac                       = false;
bool                  g_has_reconnect_target                    = false;
bool                  g_has_last_paired_device                  = false;
volatile bool         g_has_pending_paired_device               = false;
bool                  g_bt_ready                                = false;
bool                  g_hfp_ready                               = false;
bool                  g_data_callback_ready                     = false;
volatile bool         g_hfp_profile_ready                       = false;
volatile bool         g_hfp_profile_failed                      = false;
bool                  g_disconnect_requested                    = false;
volatile bool         g_hfp_audio_connecting                    = false;
volatile bool         g_hfp_audio_connected                     = false;
volatile bool         g_hfp_slc_connected                       = false;
volatile bool         g_hfp_call_active                         = false;
volatile bool         g_hfp_call_setup_active                   = false;
volatile bool         g_hfp_answer_requested                    = false;
uint32_t              g_hfp_audio_connect_started_ms            = 0;
uint32_t              g_next_reconnect_attempt_ms               = 0;
uint32_t              g_reconnect_attempt_count                 = 0;
uint32_t              g_incoming_audio_callback_count           = 0;
uint32_t              g_outgoing_audio_callback_count           = 0;
uint32_t              g_last_incoming_audio_log_ms              = 0;
uint32_t              g_last_outgoing_audio_log_ms              = 0;
uint64_t              g_incoming_audio_bytes                    = 0;
uint64_t              g_outgoing_audio_requested_bytes          = 0;
uint64_t              g_outgoing_audio_returned_bytes           = 0;
TaskHandle_t          g_audio_connect_task                      = nullptr;
IncomingAudioCallback g_incoming_audio                          = nullptr;
OutgoingAudioCallback g_outgoing_audio                          = nullptr;
PairedCallback        g_paired_callback                         = nullptr;
StateChangedCallback  g_state_changed_callback                  = nullptr;
char                  g_local_device_name[kDeviceNameMaxLength] = {};

char g_legacy_pin[kLegacyPinMaxLength + 1] = {'0', '0', '0', '0', '\0'};
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

    DBG_LOGI(TAG, "bt state: %s -> %s", stateName(g_state), stateName(next));
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

void remember_reconnect_target(const esp_bd_addr_t mac)
{
    copy_bda(g_reconnect_target_mac, mac);
    g_has_reconnect_target = true;
}

void clear_reconnect_schedule(bool clear_target)
{
    g_next_reconnect_attempt_ms = 0;
    g_reconnect_attempt_count   = 0;
    if (clear_target)
    {
        g_has_reconnect_target = false;
        memset(g_reconnect_target_mac, 0, sizeof(g_reconnect_target_mac));
    }
}

bool should_log_audio_callback(uint32_t count, uint32_t& last_log_ms)
{
    const uint32_t now = millis();
    if (count <= 5 || static_cast<uint32_t>(now - last_log_ms) >= 1000)
    {
        last_log_ms = now;
        return true;
    }

    return false;
}

void reset_audio_callback_sequence()
{
    g_incoming_audio_callback_count  = 0;
    g_outgoing_audio_callback_count  = 0;
    g_last_incoming_audio_log_ms     = 0;
    g_last_outgoing_audio_log_ms     = 0;
    g_incoming_audio_bytes           = 0;
    g_outgoing_audio_requested_bytes = 0;
    g_outgoing_audio_returned_bytes  = 0;
}

void log_local_identity(const char* reason, const char* device_name = nullptr)
{
    esp_bd_addr_t   local = {};
    const esp_err_t err   = esp_read_mac(local, ESP_MAC_BT);
    if (err != ESP_OK)
    {
        DBG_LOGW(TAG, "%s: could not read local Bluetooth BDADDR: %s", reason, esp_err_to_name(err));
        return;
    }

    char bdaddr_text[18] = {};
    format_bdaddr(local, bdaddr_text, sizeof(bdaddr_text));
    if (device_name && device_name[0] != '\0')
    {
        DBG_LOGI(TAG, "%s: local BDADDR=%s device_name=\"%s\"", reason, bdaddr_text, device_name);
    }
    else
    {
        DBG_LOGI(TAG, "%s: local BDADDR=%s", reason, bdaddr_text);
    }
}

void schedule_reconnect_attempt(uint32_t delay_ms)
{
    g_next_reconnect_attempt_ms = millis() + delay_ms;
}

bool close_pairing_window()
{
    return ok(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE), "close bt discovery window");
}

bool make_bluetooth_non_connectable()
{
    return ok(esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE), "close bt radio window");
}

void settle_outbound_connect_scan_mode()
{
    DBG_LOGI(TAG,
             "settling radio scan mode before outbound HFP connect for %" PRIu32 " ms",
             kOutboundConnectScanModeSettleMs);
    vTaskDelay(pdMS_TO_TICKS(kOutboundConnectScanModeSettleMs));
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
    case ESP_HF_CLIENT_PROF_STATE_EVT:
        return "ESP_HF_CLIENT_PROF_STATE_EVT";
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

const char* hfp_profile_state_name(esp_hf_prof_state_t state)
{
    switch (state)
    {
    case ESP_HF_INIT_SUCCESS:
        return "ESP_HF_INIT_SUCCESS";
    case ESP_HF_INIT_ALREADY:
        return "ESP_HF_INIT_ALREADY";
    case ESP_HF_INIT_FAIL:
        return "ESP_HF_INIT_FAIL";
    case ESP_HF_DEINIT_SUCCESS:
        return "ESP_HF_DEINIT_SUCCESS";
    case ESP_HF_DEINIT_ALREADY:
        return "ESP_HF_DEINIT_ALREADY";
    case ESP_HF_DEINIT_FAIL:
        return "ESP_HF_DEINIT_FAIL";
    default:
        return "UNKNOWN";
    }
}

bool configure_eir_data()
{
    esp_bt_eir_data_t eir = {};
    eir.fec_required      = false;
    eir.include_name      = true;
    eir.include_txpower   = true;
    eir.include_uuid      = true;
    eir.flag              = ESP_BT_EIR_FLAG_GEN_DISC;
    return strict_ok(esp_bt_gap_config_eir_data(&eir), "bt eir data");
}

bool ensure_controller_enabled()
{
    esp_bt_controller_status_t status = esp_bt_controller_get_status();
    DBG_LOGI(TAG, "bt controller status: %s", controller_status_name(status));

    if (btStarted())
    {
        DBG_LOGI(TAG, "Arduino btStarted: yes");
        return true;
    }

    DBG_LOGI(TAG, "Arduino btStart");
    if (!btStart())
    {
        status = esp_bt_controller_get_status();
        DBG_LOGE(TAG, "Arduino btStart failed, controller status: %s", controller_status_name(status));
        return false;
    }

    status = esp_bt_controller_get_status();
    DBG_LOGI(TAG, "bt controller status after Arduino btStart: %s", controller_status_name(status));
    return status == ESP_BT_CONTROLLER_STATUS_ENABLED;
}

bool ensure_bluedroid_enabled()
{
    esp_bluedroid_status_t status = esp_bluedroid_get_status();
    DBG_LOGI(TAG, "bluedroid status: %s", bluedroid_status_name(status));

    if (status == ESP_BLUEDROID_STATUS_UNINITIALIZED)
    {
        if (!strict_ok(esp_bluedroid_init(), "bluedroid init"))
        {
            return false;
        }
        status = esp_bluedroid_get_status();
        DBG_LOGI(TAG, "bluedroid status after init: %s", bluedroid_status_name(status));
    }

    if (status == ESP_BLUEDROID_STATUS_INITIALIZED)
    {
        if (!strict_ok(esp_bluedroid_enable(), "bluedroid enable"))
        {
            return false;
        }
        status = esp_bluedroid_get_status();
        DBG_LOGI(TAG, "bluedroid status after enable: %s", bluedroid_status_name(status));
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
    ++g_outgoing_audio_callback_count;
    g_outgoing_audio_requested_bytes += len;
    const uint32_t returned = g_outgoing_audio ? g_outgoing_audio(buf, len) : 0;
    g_outgoing_audio_returned_bytes += returned;

    if (should_log_audio_callback(g_outgoing_audio_callback_count, g_last_outgoing_audio_log_ms))
    {
        DBG_LOGI(TAG,
                 "hfp outgoing audio callback: count=%" PRIu32 " requested=%" PRIu32 " returned=%" PRIu32
                 " total_requested=%" PRIu64 " total_returned=%" PRIu64 " state=%s audio_connected=%u",
                 g_outgoing_audio_callback_count,
                 len,
                 returned,
                 g_outgoing_audio_requested_bytes,
                 g_outgoing_audio_returned_bytes,
                 stateName(g_state),
                 g_hfp_audio_connected ? 1U : 0U);
    }

    return returned;
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
    ++g_incoming_audio_callback_count;
    g_incoming_audio_bytes += len;

    if (should_log_audio_callback(g_incoming_audio_callback_count, g_last_incoming_audio_log_ms))
    {
        DBG_LOGI(TAG,
                 "hfp incoming audio callback: count=%" PRIu32 " len=%" PRIu32 " total=%" PRIu64
                 " state=%s audio_connected=%u",
                 g_incoming_audio_callback_count,
                 len,
                 g_incoming_audio_bytes,
                 stateName(g_state),
                 g_hfp_audio_connected ? 1U : 0U);
    }

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

bool wait_for_hfp_profile_ready()
{
    const uint32_t start = millis();
    while (!g_hfp_profile_ready && !g_hfp_profile_failed)
    {
        if (static_cast<uint32_t>(millis() - start) >= kHfpProfileInitTimeoutMs)
        {
            DBG_LOGE(TAG, "timed out waiting for HFP profile init");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return g_hfp_profile_ready && !g_hfp_profile_failed;
}

void reset_hfp_profile_ready()
{
    g_hfp_profile_ready  = false;
    g_hfp_profile_failed = false;
}

bool hfp_audio_connect_timed_out()
{
    return g_hfp_audio_connecting && (millis() - g_hfp_audio_connect_started_ms) >= kAudioConnectTimeoutMs;
}

esp_err_t connect_hfp_audio_once(const char* reason)
{
    if (!g_has_connected_mac)
    {
        DBG_LOGW(TAG, "hfp audio connect skipped (%s): no connected MAC", reason);
        return ESP_ERR_INVALID_STATE;
    }

    if (g_hfp_audio_connected)
    {
        DBG_LOGI(TAG, "hfp audio connect skipped (%s): already connected", reason);
        return ESP_OK;
    }

    if (g_hfp_audio_connecting)
    {
        if (!hfp_audio_connect_timed_out())
        {
            DBG_LOGI(TAG, "hfp audio connect skipped (%s): already connecting", reason);
            return ESP_OK;
        }

        DBG_LOGW(TAG, "hfp audio connect timed out while %s, retrying", reason);
        g_hfp_audio_connecting = false;
    }

    const esp_err_t err = esp_hf_client_connect_audio(g_connected_mac);
    DBG_LOGI(TAG, "esp_hf_client_connect_audio (%s) returned: %s", reason, esp_err_to_name(err));
    if (err == ESP_OK)
    {
        g_hfp_audio_connecting         = true;
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
    DBG_LOGI(TAG, "esp_hf_client_answer_call (%s) returned: %s", reason, esp_err_to_name(err));
    if (err == ESP_OK)
    {
        g_hfp_answer_requested = true;
    }
}

void audio_connect_retry_task(void*)
{
    DBG_LOGI(TAG, "hfp audio connect retry task started");

    while (hfp_control_connected() && g_has_connected_mac && !g_hfp_audio_connected && !g_disconnect_requested &&
           g_hfp_call_active)
    {
        if (!g_hfp_audio_connecting || hfp_audio_connect_timed_out())
        {
            connect_hfp_audio_once("retry task");
        }
        vTaskDelay(pdMS_TO_TICKS(kAudioConnectRetryMs));
    }

    DBG_LOGI(TAG, "hfp audio connect retry task stopped");
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
        DBG_LOGE(TAG, "failed to create hfp audio connect retry task");
    }
}

const char* connected_host_name(const esp_bd_addr_t mac)
{
    for (size_t i = 0; i < g_host_list.size(); ++i)
    {
        const bt_host_item_t* item = g_host_list.get(i);
        const char*           name = bt_host_display_name(item);
        if (item && name[0] != '\0' && bda_equal(item->bdaddr, mac))
        {
            return name;
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
    DBG_LOGI(TAG, "querying HFP call metadata: %s", reason);
    ok(esp_hf_client_query_current_calls(), "hfp query current calls");
}

void handle_pending_paired_device()
{
    if (!g_has_pending_paired_device)
    {
        return;
    }

    const PairedDevice device   = g_pending_paired_device;
    g_has_pending_paired_device = false;

    g_host_list.insert(device.name, device.mac);

    if (g_paired_callback)
    {
        g_paired_callback(device);
    }

    if (g_state == State::Pairing)
    {
        close_pairing_window();
        set_state(State::Idle);
    }
}

void gap_event(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param)
{
    switch (event)
    {
    case ESP_BT_GAP_CONFIG_EIR_DATA_EVT:
        if (param)
        {
            char   type_text[3 * ESP_BT_EIR_TYPE_MAX_NUM + 1] = {};
            size_t used                                       = 0;
            for (uint8_t i = 0;
                 i < param->config_eir_data.eir_type_num && i < ESP_BT_EIR_TYPE_MAX_NUM && used < sizeof(type_text);
                 ++i)
            {
                const int written = snprintf(type_text + used,
                                             sizeof(type_text) - used,
                                             "%s%02X",
                                             i == 0 ? "" : " ",
                                             param->config_eir_data.eir_type[i]);
                if (written <= 0)
                {
                    break;
                }
                used += static_cast<size_t>(written);
            }
            DBG_LOGI(TAG,
                     "bt gap EIR config: status=%d types=%u [%s]",
                     static_cast<int>(param->config_eir_data.stat),
                     static_cast<unsigned>(param->config_eir_data.eir_type_num),
                     type_text);
        }
        break;

    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (!param)
        {
            return;
        }

        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
        {
            g_last_paired_device = {};
            copy_bda(g_last_paired_device.mac, param->auth_cmpl.bda);
            strlcpy(g_last_paired_device.name,
                    reinterpret_cast<const char*>(param->auth_cmpl.device_name),
                    sizeof(g_last_paired_device.name));
            g_has_last_paired_device = true;
            log_bda("paired with", g_last_paired_device.mac);
            g_pending_paired_device     = g_last_paired_device;
            g_has_pending_paired_device = true;
        }
        else
        {
            DBG_LOGW(TAG, "pairing failed, status=%d", param->auth_cmpl.stat);
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
            DBG_LOGI(TAG, "confirming SSP numeric comparison: %" PRIu32, param->cfm_req.num_val);
            ok(esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true), "ssp confirm reply");
        }
        break;

    case ESP_BT_GAP_KEY_NOTIF_EVT:
        if (param)
        {
            DBG_LOGI(TAG, "SSP passkey: %" PRIu32, param->key_notif.passkey);
        }
        break;

    case ESP_BT_GAP_KEY_REQ_EVT:
        DBG_LOGW(TAG, "remote requested passkey entry; this device has no keyboard");
        break;

    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        if (param)
        {
            char bdaddr_text[18] = {};
            format_bdaddr(param->acl_conn_cmpl_stat.bda, bdaddr_text, sizeof(bdaddr_text));
            DBG_LOGI(TAG,
                     "bt gap ACL connected: status=%d handle=%u bdaddr=%s",
                     static_cast<int>(param->acl_conn_cmpl_stat.stat),
                     static_cast<unsigned>(param->acl_conn_cmpl_stat.handle),
                     bdaddr_text);
        }
        break;

    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        if (param)
        {
            char bdaddr_text[18] = {};
            format_bdaddr(param->acl_disconn_cmpl_stat.bda, bdaddr_text, sizeof(bdaddr_text));
            DBG_LOGI(TAG,
                     "bt gap ACL disconnected: reason=%d handle=%u bdaddr=%s",
                     static_cast<int>(param->acl_disconn_cmpl_stat.reason),
                     static_cast<unsigned>(param->acl_disconn_cmpl_stat.handle),
                     bdaddr_text);
        }
        break;

    case ESP_BT_GAP_MODE_CHG_EVT:
        if (param)
        {
            char bdaddr_text[18] = {};
            format_bdaddr(param->mode_chg.bda, bdaddr_text, sizeof(bdaddr_text));
            DBG_LOGI(TAG,
                     "bt gap mode changed: mode=%d interval=%u bdaddr=%s",
                     static_cast<int>(param->mode_chg.mode),
                     static_cast<unsigned>(param->mode_chg.interval),
                     bdaddr_text);
        }
        break;

    default:
        break;
    }
}

void hfp_event(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t* param)
{
    DBG_LOGI(TAG, "hfp_event: %s (%d)", hfp_event_name(event), static_cast<int>(event));

    if (!param)
    {
        DBG_LOGW(TAG, "hfp_event %s had null param", hfp_event_name(event));
        return;
    }

    switch (event)
    {
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        DBG_LOGI(TAG,
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
            remember_reconnect_target(g_connected_mac);
            g_has_connected_mac = true;
            clear_reconnect_schedule(false);
            g_hfp_slc_connected = false;
            DBG_LOGI(TAG, "hfp RFCOMM/control link connected; waiting for service level connection");
            if (g_disconnect_requested)
            {
                ok(esp_hf_client_disconnect(g_connected_mac), "hfp disconnect after canceled connect");
                set_state(State::Idle);
                break;
            }
            set_state(State::Connecting);
            break;

        case ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED:
            copy_bda(g_connected_mac, param->conn_stat.remote_bda);
            remember_reconnect_target(g_connected_mac);
            g_has_connected_mac = true;
            clear_reconnect_schedule(false);
            if (g_disconnect_requested)
            {
                ok(esp_hf_client_disconnect(g_connected_mac), "hfp disconnect after canceled slc connect");
                set_state(State::Idle);
                break;
            }
            g_hfp_audio_connecting         = false;
            g_hfp_audio_connected          = false;
            g_hfp_slc_connected            = true;
            g_hfp_call_active              = false;
            g_hfp_call_setup_active        = false;
            g_hfp_answer_requested         = false;
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
            const bool had_slc_connected = g_hfp_slc_connected;
            const bool was_connected     = hfp_control_connected();
            const bool was_connecting    = g_state == State::Connecting;
            const bool was_reconnecting  = g_state == State::Reconnecting;
            if (!g_disconnect_requested && g_state != State::Pairing && had_slc_connected)
            {
                remember_reconnect_target(g_connected_mac);
            }
            g_has_connected_mac            = false;
            g_hfp_audio_connecting         = false;
            g_hfp_audio_connected          = false;
            g_hfp_slc_connected            = false;
            g_hfp_call_active              = false;
            g_hfp_call_setup_active        = false;
            g_hfp_answer_requested         = false;
            g_hfp_audio_connect_started_ms = 0;
            CallManager::onBluetoothDisconnected();
            if (g_disconnect_requested || g_state == State::Pairing)
            {
                g_disconnect_requested = false;
                clear_reconnect_schedule(false);
                close_pairing_window();
                set_state(State::Idle);
            }
            else if (was_connected || was_reconnecting || had_slc_connected)
            {
                schedule_reconnect_attempt(kReconnectAttemptIntervalMs);
                set_state(State::Reconnecting);
            }
            else if (was_connecting)
            {
                clear_reconnect_schedule(false);
                close_pairing_window();
                set_state(State::Idle);
            }
            break;
        }
        break;

    case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        DBG_LOGI(TAG,
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
            reset_audio_callback_sequence();
            g_hfp_audio_connecting         = false;
            g_hfp_audio_connected          = false;
            g_hfp_audio_connect_started_ms = 0;
            AudioManager::setHfpAudioFormat(AudioManager::HfpCodec::Cvsd, 8000);
            g_hfp_audio_connected = true;
            CallManager::setScoAudioConnected(true);
            set_state(State::AudioAvailable);
            DBG_LOGI(TAG, "HFP CVSD/narrowband audio connected");
        }
        else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC)
        {
            reset_audio_callback_sequence();
            g_hfp_audio_connecting         = false;
            g_hfp_audio_connected          = false;
            g_hfp_audio_connect_started_ms = 0;
            AudioManager::setHfpAudioFormat(AudioManager::HfpCodec::Msbc, AudioManager::kSampleRateHz);
            g_hfp_audio_connected = true;
            CallManager::setScoAudioConnected(true);
            set_state(State::AudioAvailable);
            DBG_LOGI(TAG, "HFP mSBC/wideband audio connected");
        }
        else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED && hfp_control_connected() &&
                 g_has_connected_mac)
        {
            DBG_LOGI(TAG,
                     "hfp audio callback summary: incoming=%" PRIu32 "/%" PRIu64 " bytes outgoing=%" PRIu32
                     " requested=%" PRIu64 " returned=%" PRIu64,
                     g_incoming_audio_callback_count,
                     g_incoming_audio_bytes,
                     g_outgoing_audio_callback_count,
                     g_outgoing_audio_requested_bytes,
                     g_outgoing_audio_returned_bytes);
            g_hfp_audio_connecting         = false;
            g_hfp_audio_connected          = false;
            g_hfp_audio_connect_started_ms = 0;
            AudioManager::setHfpAudioFormat(AudioManager::HfpCodec::Cvsd, 8000);
            CallManager::setScoAudioConnected(false);
            set_state(State::Connected);
        }
        break;

    case ESP_HF_CLIENT_CIND_CALL_EVT:
        DBG_LOGI(TAG, "hfp call status: %d", static_cast<int>(param->call.status));
        CallManager::setCallStatus(static_cast<int>(param->call.status));
        g_hfp_call_active = param->call.status != 0;
        if (!g_hfp_call_active)
        {
            g_hfp_call_setup_active = false;
            g_hfp_answer_requested  = false;
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
        DBG_LOGI(TAG, "hfp call setup status: %d", static_cast<int>(param->call_setup.status));
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
        DBG_LOGI(TAG, "hfp call held status: %d", static_cast<int>(param->call_held.status));
        CallManager::setCallHeldStatus(static_cast<int>(param->call_held.status));
        break;

    case ESP_HF_CLIENT_AT_RESPONSE_EVT:
        DBG_LOGI(TAG,
                 "hfp AT response: code=%d cme=%d",
                 static_cast<int>(param->at_response.code),
                 static_cast<int>(param->at_response.cme));
        break;

    case ESP_HF_CLIENT_CLIP_EVT:
        DBG_LOGI(TAG, "hfp caller id: %s", param->clip.number ? param->clip.number : "(null)");
        CallManager::addCallerInfo(param->clip.number);
        break;

    case ESP_HF_CLIENT_CCWA_EVT:
        DBG_LOGI(TAG, "hfp call waiting: %s", param->ccwa.number ? param->ccwa.number : "(null)");
        CallManager::addCallerInfo(param->ccwa.number);
        break;

    case ESP_HF_CLIENT_CLCC_EVT:
        DBG_LOGI(TAG,
                 "hfp current call: idx=%d dir=%d status=%d mpty=%d number=%s",
                 param->clcc.idx,
                 static_cast<int>(param->clcc.dir),
                 static_cast<int>(param->clcc.status),
                 static_cast<int>(param->clcc.mpty),
                 param->clcc.number ? param->clcc.number : "(null)");
        CallManager::addCallerInfo(param->clcc.number);
        break;

    case ESP_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT:
        DBG_LOGI(TAG, "hfp service availability: %d", static_cast<int>(param->service_availability.status));
        break;

    case ESP_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT:
        DBG_LOGI(TAG, "hfp signal strength: %d", param->signal_strength.value);
        break;

    case ESP_HF_CLIENT_CIND_ROAMING_STATUS_EVT:
        DBG_LOGI(TAG, "hfp roaming status: %d", static_cast<int>(param->roaming.status));
        break;

    case ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT:
        DBG_LOGI(TAG, "hfp battery level: %d", param->battery_level.value);
        break;

    case ESP_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT:
        DBG_LOGI(TAG, "hfp operator: %s", param->cops.name ? param->cops.name : "(null)");
        break;

    case ESP_HF_CLIENT_BVRA_EVT:
        DBG_LOGI(TAG, "hfp voice recognition state: %d", static_cast<int>(param->bvra.value));
        break;

    case ESP_HF_CLIENT_BTRH_EVT:
        DBG_LOGI(TAG, "hfp response and hold status: %d", static_cast<int>(param->btrh.status));
        break;

    case ESP_HF_CLIENT_CNUM_EVT:
        DBG_LOGI(TAG,
                 "hfp subscriber number: %s type=%d",
                 param->cnum.number ? param->cnum.number : "(null)",
                 static_cast<int>(param->cnum.type));
        break;

    case ESP_HF_CLIENT_BSIR_EVT:
        DBG_LOGI(TAG, "hfp in-band ringtone state: %d", static_cast<int>(param->bsir.state));
        break;

    case ESP_HF_CLIENT_BINP_EVT:
        DBG_LOGI(TAG, "hfp voice tag number: %s", param->binp.number ? param->binp.number : "(null)");
        CallManager::addCallerInfo(param->binp.number);
        break;

    case ESP_HF_CLIENT_RING_IND_EVT:
        DBG_LOGI(TAG, "hfp ring indication");
        g_hfp_call_setup_active = true;
        CallManager::setCallSetupStatus(ESP_HF_CALL_SETUP_STATUS_INCOMING);
        query_call_metadata("ring indication");
        answer_incoming_call_once("ring indication");
        break;

    case ESP_HF_CLIENT_PKT_STAT_NUMS_GET_EVT:
        DBG_LOGI(TAG,
                 "hfp packet stats: rx_total=%" PRIu32 " rx_correct=%" PRIu32 " rx_err=%" PRIu32 " rx_none=%" PRIu32
                 " rx_lost=%" PRIu32 " tx_total=%" PRIu32 " tx_discarded=%" PRIu32,
                 param->pkt_nums.rx_total,
                 param->pkt_nums.rx_correct,
                 param->pkt_nums.rx_err,
                 param->pkt_nums.rx_none,
                 param->pkt_nums.rx_lost,
                 param->pkt_nums.tx_total,
                 param->pkt_nums.tx_discarded);
        break;

    case ESP_HF_CLIENT_PROF_STATE_EVT:
        DBG_LOGI(TAG,
                 "hfp profile state: %s (%d)",
                 hfp_profile_state_name(param->prof_stat.state),
                 static_cast<int>(param->prof_stat.state));
        if (param->prof_stat.state == ESP_HF_INIT_SUCCESS || param->prof_stat.state == ESP_HF_INIT_ALREADY)
        {
            g_hfp_profile_ready  = true;
            g_hfp_profile_failed = false;
            configure_eir_data();
        }
        else if (param->prof_stat.state == ESP_HF_INIT_FAIL)
        {
            g_hfp_profile_ready   = false;
            g_hfp_profile_failed  = true;
            g_hfp_ready           = false;
            g_data_callback_ready = false;
        }
        else if (param->prof_stat.state == ESP_HF_DEINIT_SUCCESS || param->prof_stat.state == ESP_HF_DEINIT_ALREADY)
        {
            reset_hfp_profile_ready();
            g_hfp_ready           = false;
            g_data_callback_ready = false;
        }
        break;

    case ESP_HF_CLIENT_VOLUME_CONTROL_EVT:
        if (param->volume_control.type == ESP_HF_VOLUME_CONTROL_TARGET_SPK)
        {
            const int  clamped_hfp_volume = constrain(param->volume_control.volume, 0, kHfpMaxVolume);
            const auto audio_volume       = static_cast<uint8_t>(clamped_hfp_volume * 2);
            AudioManager::setVolume(audio_volume);
            DBG_LOGI(TAG, "speaker volume set from HFP: %d -> %u", clamped_hfp_volume, audio_volume);
        }
        else if (param->volume_control.type == ESP_HF_VOLUME_CONTROL_TARGET_MIC)
        {
            DBG_LOGI(TAG, "HFP microphone gain request ignored: %d", param->volume_control.volume);
        }
        break;

    default:
        DBG_LOGI(TAG, "hfp event has no detailed logger yet: %s (%d)", hfp_event_name(event), static_cast<int>(event));
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
    const char* selected_device_name = device_name ? device_name : formatted_device_name;

    log_local_identity("initializing BtManager", selected_device_name);
    if (!strict_ok(esp_bt_sleep_disable(), "bt sleep disable") ||
        !strict_ok(esp_bt_gap_register_callback(gap_event), "gap callback") ||
        !strict_ok(esp_bt_gap_set_device_name(selected_device_name), "bt name") ||
        !strict_ok(esp_bt_gap_set_cod(handsfree_cod(), ESP_BT_INIT_COD), "bt class of device") ||
        !strict_ok(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap)), "ssp iocap") ||
        !strict_ok(esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, pin_length, pin), "legacy pin") ||
        !strict_ok(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE), "initial scan mode") ||
        !strict_ok(esp_bredr_sco_datapath_set(ESP_SCO_DATA_PATH_HCI), "sco hci path"))
    {
        return false;
    }

    configure_eir_data();
    strlcpy(g_local_device_name, selected_device_name, sizeof(g_local_device_name));
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
        reset_hfp_profile_ready();
        if (!ok(esp_hf_client_register_callback(hfp_event), "hfp callback") || !ok(esp_hf_client_init(), "hfp init"))
        {
            return false;
        }
        g_hfp_ready = true;
    }

    if (!wait_for_hfp_profile_ready())
    {
        ok(esp_hf_client_deinit(), "hfp deinit after init timeout");
        g_hfp_ready           = false;
        g_data_callback_ready = false;
        reset_hfp_profile_ready();
        return false;
    }

    return register_data_callbacks_if_ready();
}

bool deinit_hfp_for_pairing()
{
    if (!g_hfp_ready)
    {
        return true;
    }

    if (!ok(esp_hf_client_deinit(), "hfp deinit before pairing"))
    {
        return false;
    }

    g_hfp_ready           = false;
    g_data_callback_ready = false;
    reset_hfp_profile_ready();
    return true;
}

} // namespace

bool init(const char*           deviceName,
          IncomingAudioCallback incomingAudio,
          OutgoingAudioCallback outgoingAudio,
          const char*           pin)
{
    g_incoming_audio = incomingAudio;
    g_outgoing_audio = outgoingAudio;
    return init_hfp(deviceName, pin);
}

bool initBluetoothOnly(const char* deviceName, const char* pin)
{
    return init_bluetooth(deviceName, pin);
}

void setAudioCallbacks(IncomingAudioCallback incomingAudio, OutgoingAudioCallback outgoingAudio)
{
    g_incoming_audio = incomingAudio;
    g_outgoing_audio = outgoingAudio;
    if (g_hfp_ready)
    {
        register_data_callbacks_if_ready();
    }
}

void poll()
{
    handle_pending_paired_device();

    if (g_state != State::Reconnecting || g_disconnect_requested || g_has_connected_mac || !g_bt_ready ||
        !g_hfp_ready || !g_has_reconnect_target)
    {
        return;
    }

    const uint32_t now = millis();
    if (g_next_reconnect_attempt_ms != 0 && static_cast<int32_t>(now - g_next_reconnect_attempt_ms) < 0)
    {
        return;
    }

    g_next_reconnect_attempt_ms = now + kReconnectAttemptIntervalMs;
    ++g_reconnect_attempt_count;
    DBG_LOGI(TAG, "automatic HFP reconnect attempt %" PRIu32, g_reconnect_attempt_count);
    log_bda("reconnecting HFP to", g_reconnect_target_mac);

    if (!close_pairing_window())
    {
        DBG_LOGW(TAG, "automatic HFP reconnect could not close discovery window first");
        return;
    }
    settle_outbound_connect_scan_mode();

    const esp_err_t err = esp_hf_client_connect(g_reconnect_target_mac);
    if (err == ESP_OK)
    {
        set_state(State::Connecting);
    }
    else
    {
        DBG_LOGW(TAG, "automatic HFP reconnect attempt returned: %s", esp_err_to_name(err));
    }
}

bool generateLegacyPinFromMac()
{
    esp_bd_addr_t mac      = {};
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

    char target_bdaddr_text[18] = {};
    format_bdaddr(mac, target_bdaddr_text, sizeof(target_bdaddr_text));
    log_local_identity("starting HFP connection");
    DBG_LOGI(TAG, "starting HFP connection: remote BDADDR=%s", target_bdaddr_text);

    if (!bonded_mac_matches(mac))
    {
        return Result::NotBonded;
    }

    if (g_state != State::Idle && g_state != State::Reconnecting)
    {
        return Result::Busy;
    }

    copy_bda(g_target_mac, mac);
    remember_reconnect_target(g_target_mac);
    clear_reconnect_schedule(false);
    g_hfp_slc_connected    = false;
    g_disconnect_requested = false;
    set_state(State::Connecting);
    if (!close_pairing_window())
    {
        set_state(State::Idle);
        return Result::EspError;
    }

    settle_outbound_connect_scan_mode();
    const esp_err_t err = esp_hf_client_connect(g_target_mac);
    if (err != ESP_OK)
    {
        close_pairing_window();
        set_state(State::Idle);
    }
    return result_from_esp(err, "hfp connect");
}

Result startPairing()
{
    if (!init_hfp(nullptr, nullptr))
    {
        return Result::InitFailed;
    }

    log_local_identity("starting pairing", g_local_device_name);
    if (hfp_control_connected() || g_state == State::Connecting)
    {
        return Result::Busy;
    }

    g_disconnect_requested   = false;
    g_has_last_paired_device = false;
    clear_reconnect_schedule(false);
    set_state(State::Pairing);
    return result_from_esp(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE),
                           "open pairing window");
}

Result disconnect()
{
    if (!g_bt_ready)
    {
        clear_reconnect_schedule(false);
        g_hfp_slc_connected = false;
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
    else if (g_state == State::Connecting && g_has_reconnect_target)
    {
        ok(esp_hf_client_disconnect(g_reconnect_target_mac), "hfp connect cancel");
        clear_reconnect_schedule(false);
        g_hfp_slc_connected = false;
        set_state(State::Idle);
    }
    else
    {
        clear_reconnect_schedule(false);
        g_hfp_slc_connected = false;
        set_state(State::Idle);
        g_disconnect_requested = false;
    }

    return Result::Ok;
}

Result setConnectableNonDiscoverable()
{
    if (!g_bt_ready)
    {
        return Result::InitFailed;
    }

    return result_from_esp(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE),
                           "set bt connectable/non-discoverable");
}

Result setNonConnectableNonDiscoverable()
{
    if (!g_bt_ready)
    {
        return Result::InitFailed;
    }

    return result_from_esp(esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE),
                           "set bt non-connectable/non-discoverable");
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
    clear_reconnect_schedule(false);
    g_hfp_audio_connecting         = false;
    g_hfp_audio_connected          = false;
    g_hfp_slc_connected            = false;
    g_hfp_call_active              = false;
    g_hfp_call_setup_active        = false;
    g_hfp_answer_requested         = false;
    g_hfp_audio_connect_started_ms = 0;
    g_audio_connect_task           = nullptr;
    g_hfp_ready                    = false;
    g_bt_ready                     = false;
    g_data_callback_ready          = false;
    g_disconnect_requested         = false;
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

const char* localDeviceName()
{
    return g_local_device_name[0] != '\0' ? g_local_device_name : "The Fly";
}

bool localBdaddr(esp_bd_addr_t bdaddr)
{
    return bdaddr && esp_read_mac(bdaddr, ESP_MAC_BT) == ESP_OK;
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
