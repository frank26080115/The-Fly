#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <inttypes.h>
#include <stdlib.h>

#include "AudioManager.h"
#include "BluetoothManager.h"
#include "esp_bt_defs.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "utilfuncs.h"

//#define USE_SPECIFIC_BDADDR    "F4:26:79:C6:FA:01" // PC
#define USE_SPECIFIC_BDADDR    "58:79:e0:36:81:59" // phone
// #define USE_SPECIFIC_BONDED_IDX    0

#if defined(USE_SPECIFIC_BDADDR) && defined(USE_SPECIFIC_BONDED_IDX)
#error "Use only one of USE_SPECIFIC_BDADDR or USE_SPECIFIC_BONDED_IDX"
#endif

namespace
{

constexpr const char* TAG = "test_btspeakerphone";
constexpr const char* kApSsid = "TheFly-BT-Web-Test";
constexpr const char* kHelloWorld = "hello world\n";
constexpr uint32_t    kCore0StackSize = 8192;
constexpr UBaseType_t kCore0Priority  = 2;
constexpr uint32_t    kButtonPollMs   = 5;
#ifdef ENABLE_HFP_AUDIO_DIAGNOSTICS
constexpr uint32_t    kAudioDiagReportMs = 2000;
#endif
constexpr uint16_t    kColourGrey     = 0x7BEF;

enum class ControlCommand : uint8_t
{
    EnableSpeaker,
    EnableMic,
    Stop,
};

AsyncWebServer g_web_server(80);
TaskHandle_t   g_core0_task = nullptr;
QueueHandle_t  g_control_queue = nullptr;
QueueHandle_t  g_colour_queue  = nullptr;

#ifdef ENABLE_HFP_AUDIO_DIAGNOSTICS
const char* hfp_codec_name(AudioManager::HfpCodec codec)
{
    switch (codec)
    {
    case AudioManager::HfpCodec::Cvsd:
        return "CVSD";
    case AudioManager::HfpCodec::Msbc:
        return "mSBC";
    default:
        return "unknown";
    }
}

uint32_t delta32(uint32_t current, uint32_t previous)
{
    return current - previous;
}

uint64_t delta64(uint64_t current, uint64_t previous)
{
    return current - previous;
}

void log_audio_diagnostics(const AudioManager::HfpAudioDiagnostics& diag, const AudioManager::HfpAudioDiagnostics& previous)
{
    ESP_LOGI(TAG,
             "audio diag in: cb=%" PRIu32 " (+%" PRIu32 ") bytes=%" PRIu64 " (+%" PRIu64 ") consumed=%" PRIu64 " pcm=%" PRIu64 " q_spk=%" PRIu64 " dec_frames=%" PRIu32 " dec_fail=%" PRIu32 " no_dec=%" PRIu32,
             diag.incomingCallbacks,
             delta32(diag.incomingCallbacks, previous.incomingCallbacks),
             diag.incomingBytes,
             delta64(diag.incomingBytes, previous.incomingBytes),
             diag.incomingConsumedBytes,
             diag.incomingPcmSamples,
             diag.incomingQueuedSpkSamples,
             diag.incomingDecodeFrames,
             diag.incomingDecodeFailures,
             diag.incomingNoDecoder);

    ESP_LOGI(TAG,
             "audio diag out: cb=%" PRIu32 " (+%" PRIu32 ") req=%" PRIu64 " (+%" PRIu64 ") ret=%" PRIu64 " (+%" PRIu64 ") pcm_read=%" PRIu64 " underflow=%" PRIu32 " enc_frames=%" PRIu32 " enc_fail=%" PRIu32 " no_enc=%" PRIu32,
             diag.outgoingCallbacks,
             delta32(diag.outgoingCallbacks, previous.outgoingCallbacks),
             diag.outgoingRequestedBytes,
             delta64(diag.outgoingRequestedBytes, previous.outgoingRequestedBytes),
             diag.outgoingReturnedBytes,
             delta64(diag.outgoingReturnedBytes, previous.outgoingReturnedBytes),
             diag.outgoingPcmSamplesRead,
             diag.outgoingUnderflows,
             diag.outgoingEncodeFrames,
             diag.outgoingEncodeFailures,
             diag.outgoingNoEncoder);

    ESP_LOGI(TAG,
             "audio diag spk: i2s_bytes=%" PRIu64 " (+%" PRIu64 ") i2s_frames=%" PRIu64 " chunks=%" PRIu64 " short=%" PRIu32 " err=%" PRIu32 " fifo=%u%%/%u samples mode=%d codec=%s rate=%lu",
             diag.speakerI2sWriteBytes,
             delta64(diag.speakerI2sWriteBytes, previous.speakerI2sWriteBytes),
             diag.speakerI2sWriteFrames,
             diag.speakerPumpCalls,
             diag.speakerI2sShortWrites,
             diag.speakerI2sWriteErrors,
             static_cast<unsigned>(AudioManager::bluetoothToSpeakerFifo().getFillPercentage()),
             static_cast<unsigned>(AudioManager::bluetoothToSpeakerFifo().availableToRead()),
             static_cast<int>(AudioManager::mode()),
             hfp_codec_name(AudioManager::hfpAudioCodec()),
             static_cast<unsigned long>(AudioManager::hfpAudioSampleRateHz()));

    ESP_LOGI(TAG,
             "audio diag mic: pump=%" PRIu32 " (+%" PRIu32 ") skip mode/no_i2s/full=%" PRIu32 "/%" PRIu32 "/%" PRIu32 " i2s=%" PRIu32 " (+%" PRIu32 ") req=%" PRIu64 " (+%" PRIu64 ") read=%" PRIu64 " (+%" PRIu64 ") empty=%" PRIu32 " err=%" PRIu32 " samp=%" PRIu64 " q_bt=%" PRIu64 " (+%" PRIu64 ") q_file=%" PRIu64 " bt_drop notready/full=%" PRIu64 "/%" PRIu64 " notify chk/ready/call=%" PRIu32 "/%" PRIu32 "/%" PRIu32 " block q/min/bt=%" PRIu32 "/%" PRIu32 "/%" PRIu32 " fifo=%u%%/%u min=%" PRIu32 " can_bt=%u",
             diag.micPumpCalls,
             delta32(diag.micPumpCalls, previous.micPumpCalls),
             diag.micSkipNotMicMode,
             diag.micSkipNoI2s,
             diag.micSkipFifoFull,
             diag.micI2sReadCalls,
             delta32(diag.micI2sReadCalls, previous.micI2sReadCalls),
             diag.micI2sRequestedBytes,
             delta64(diag.micI2sRequestedBytes, previous.micI2sRequestedBytes),
             diag.micI2sReadBytes,
             delta64(diag.micI2sReadBytes, previous.micI2sReadBytes),
             diag.micI2sReadEmpty,
             diag.micI2sReadErrors,
             diag.micI2sReadSamples,
             diag.micQueuedBtSamples,
             delta64(diag.micQueuedBtSamples, previous.micQueuedBtSamples),
             diag.micQueuedFileSamples,
             diag.micBtNotReadySamples,
             diag.micBtFifoFullSamples,
             diag.micNotifyChecks,
             diag.micNotifyReady,
             diag.micNotifyCalls,
             diag.micNotifyNoQueued,
             diag.micNotifyBelowMin,
             diag.micNotifyBtNotReady,
             static_cast<unsigned>(AudioManager::micToBluetoothFifo().getFillPercentage()),
             static_cast<unsigned>(AudioManager::micToBluetoothFifo().availableToRead()),
             diag.micNotifyMinSamples,
             BtManager::canNotifyOutgoingAudioReady() ? 1U : 0U);
}
#endif

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

bool init_control_queues()
{
    if (!g_control_queue)
    {
        g_control_queue = xQueueCreate(8, sizeof(ControlCommand));
    }
    if (!g_colour_queue)
    {
        g_colour_queue = xQueueCreate(1, sizeof(uint16_t));
    }

    if (!g_control_queue || !g_colour_queue)
    {
        Serial.printf("%s: failed to create FreeRTOS queues\n", TAG);
        return false;
    }

    return true;
}

void send_control_command(ControlCommand command)
{
    if (!g_control_queue)
    {
        return;
    }

    if (xQueueSend(g_control_queue, &command, 0) != pdTRUE)
    {
        Serial.printf("%s: control command queue full\n", TAG);
    }
}

void send_display_colour(uint16_t colour)
{
    if (!g_colour_queue)
    {
        return;
    }

    xQueueOverwrite(g_colour_queue, &colour);
}

void print_bonded_devices()
{
    const int bonded_count = esp_bt_gap_get_bond_device_num();
    if (bonded_count == ESP_ERR_INVALID_STATE)
    {
        Serial.printf("%s: esp_bt_gap_get_bond_device_num failed: %s\n", TAG, esp_err_to_name(static_cast<esp_err_t>(bonded_count)));
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

    int listed = bonded_count;
    const esp_err_t err = esp_bt_gap_get_bond_device_list(&listed, bonded);
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

bool get_nth_bonded_device(int index, esp_bd_addr_t device)
{
    const int bonded_count = esp_bt_gap_get_bond_device_num();
    if (bonded_count == ESP_ERR_INVALID_STATE)
    {
        Serial.printf("%s: esp_bt_gap_get_bond_device_num failed: %s\n", TAG, esp_err_to_name(static_cast<esp_err_t>(bonded_count)));
        return false;
    }

    if (index < 0 || index >= bonded_count)
    {
        Serial.printf("%s: bonded device index %d out of range, count=%d\n", TAG, index, bonded_count);
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

    const bool have_device = index < listed;
    if (have_device)
    {
        memcpy(device, bonded[index], ESP_BD_ADDR_LEN);
    }
    else
    {
        Serial.printf("%s: bonded device index %d not listed, listed=%d\n", TAG, index, listed);
    }

    free(bonded);
    return have_device;
}

bool select_target_device(esp_bd_addr_t target)
{
    print_bonded_devices();

#ifdef USE_SPECIFIC_BDADDR
    if (!parse_mac(USE_SPECIFIC_BDADDR, target))
    {
        Serial.printf("%s: invalid USE_SPECIFIC_BDADDR: %s\n", TAG, USE_SPECIFIC_BDADDR);
        return false;
    }

    log_bda("using specific Bluetooth address", target);
    return true;
#else
    const int index =
#ifdef USE_SPECIFIC_BONDED_IDX
        USE_SPECIFIC_BONDED_IDX;
#else
        0;
#endif

    Serial.printf("%s: using bonded device index: %d\n", TAG, index);
    return get_nth_bonded_device(index, target);
#endif
}

void choke_file_fifos()
{
    AudioManager::bluetoothToFileFifo().clear();
    AudioManager::micToFileFifo().clear();
    AudioManager::bluetoothToFileFifo().choke();
    AudioManager::micToFileFifo().choke();
    Serial.printf("%s: file FIFOs choked\n", TAG);
}

void init_display_and_buttons()
{
    M5.Display.setBrightness(255);
    M5.Display.setColorDepth(16);
    M5.Display.fillScreen(kColourGrey);
    M5.update();
    M5.BtnA.setDebounceThresh(20);
    M5.BtnB.setDebounceThresh(20);
    M5.BtnC.setDebounceThresh(20);
}

void apply_requested_colour(uint16_t& current_colour)
{
    uint16_t requested_colour = current_colour;
    while (xQueueReceive(g_colour_queue, &requested_colour, 0) == pdTRUE)
    {
    }

    if (requested_colour != current_colour)
    {
        current_colour = requested_colour;
        M5.Display.fillScreen(current_colour);
    }
}

void btspeakerphone_core0_task(void*)
{
    bool     ptt_was_pressed = false;
    uint16_t current_colour  = kColourGrey;

    init_display_and_buttons();
    Serial.printf("%s: core 0 UI/control task started\n", TAG);

    while (true)
    {
        apply_requested_colour(current_colour);
        M5.update();

        if (M5.BtnC.wasPressed())
        {
            send_control_command(ControlCommand::Stop);
        }

        const bool ptt_pressed = M5.BtnA.isPressed();
        if (ptt_pressed != ptt_was_pressed)
        {
            send_control_command(ptt_pressed ? ControlCommand::EnableMic : ControlCommand::EnableSpeaker);
            ptt_was_pressed = ptt_pressed;
        }

        delay(kButtonPollMs);
    }
}

bool start_core0_control_task()
{
    const BaseType_t task_created = xTaskCreatePinnedToCore(btspeakerphone_core0_task,
                                                            "btspk_core0",
                                                            kCore0StackSize,
                                                            nullptr,
                                                            kCore0Priority,
                                                            &g_core0_task,
                                                            0);
    if (task_created != pdPASS)
    {
        Serial.printf("%s: failed to create core 0 task\n", TAG);
        return false;
    }

    return true;
}

bool handle_control_command(ControlCommand command, bool& stop_requested)
{
    switch (command)
    {
    case ControlCommand::EnableSpeaker:
        Serial.printf("%s: push-to-talk released, enabling speaker mode\n", TAG);
        if (!AudioManager::enableSpeakerMode())
        {
            Serial.printf("%s: enableSpeakerMode failed\n", TAG);
            send_display_colour(kColourGrey);
            BtManager::disconnect();
            AudioManager::stop();
            stop_requested = true;
            return false;
        }
        send_display_colour(BtManager::state() == BtManager::State::Connected ? TFT_BLUE : kColourGrey);
        return true;

    case ControlCommand::EnableMic:
        if (BtManager::state() != BtManager::State::Connected)
        {
            Serial.printf("%s: push-to-talk ignored, Bluetooth is not connected\n", TAG);
            return true;
        }
        Serial.printf("%s: push-to-talk down, enabling mic mode\n", TAG);
        if (!AudioManager::enableMicMode())
        {
            Serial.printf("%s: enableMicMode failed\n", TAG);
            send_display_colour(kColourGrey);
            BtManager::disconnect();
            AudioManager::stop();
            stop_requested = true;
            return false;
        }
        send_display_colour(TFT_RED);
        return true;

    case ControlCommand::Stop:
        Serial.printf("%s: stop requested: right touch button\n", TAG);
        send_display_colour(kColourGrey);
        BtManager::disconnect();
        AudioManager::stop();
        stop_requested = true;
        return true;
    }

    return true;
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
    if (!select_target_device(target))
    {
        Serial.printf("%s: no usable Bluetooth target, nothing to connect\n", TAG);
        idle_forever();
    }

    Serial.printf("%s: initializing AudioManager\n", TAG);
    if (!AudioManager::init(AudioManager::Hardware::M5StackInternal))
    {
        Serial.printf("%s: AudioManager init failed\n", TAG);
        idle_forever();
    }

    if (!init_control_queues() || !start_core0_control_task())
    {
        idle_forever();
    }
    send_display_colour(kColourGrey);

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

    Serial.printf("%s: pumping Bluetooth audio forever\n", TAG);
#ifdef ENABLE_HFP_AUDIO_DIAGNOSTICS
    AudioManager::resetHfpAudioDiagnostics();
    AudioManager::HfpAudioDiagnostics previous_diag = {};
    uint32_t last_diag_report_ms = millis();
#endif
    bool     blue_sent           = false;
    bool     stop_requested      = false;
    while (!stop_requested)
    {
        ControlCommand command = ControlCommand::EnableSpeaker;
        while (xQueueReceive(g_control_queue, &command, 0) == pdTRUE)
        {
            handle_control_command(command, stop_requested);
        }

        if (stop_requested)
        {
            break;
        }

        if (BtManager::state() == BtManager::State::Connected && !blue_sent && AudioManager::mode() != AudioManager::P2TMode::Mic)
        {
            send_display_colour(TFT_BLUE);
            blue_sent = true;
        }

        if (AudioManager::mode() == AudioManager::P2TMode::Mic)
        {
            AudioManager::pump_mic2bt();
        }
        else
        {
            AudioManager::pump_bt2spk();
        }

#ifdef ENABLE_HFP_AUDIO_DIAGNOSTICS
        const uint32_t now_ms = millis();
        if (now_ms - last_diag_report_ms >= kAudioDiagReportMs)
        {
            const AudioManager::HfpAudioDiagnostics diag = AudioManager::hfpAudioDiagnostics();
            log_audio_diagnostics(diag, previous_diag);
            previous_diag       = diag;
            last_diag_report_ms = now_ms;
        }
#endif

        taskYIELD();
    }

    Serial.printf("%s: finished, spinning forever\n", TAG);
    idle_forever();
}
