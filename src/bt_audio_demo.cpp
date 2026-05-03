#include <Arduino.h>
#include <M5Unified.h>
#include <ctype.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "nvs_flash.h"
#include "sbc.h"
#include "utilfuncs.h"

#ifndef BT_AUDIO_DEMO_HOST_MAC
#define BT_AUDIO_DEMO_HOST_MAC "00:00:00:00:00:00"
#endif

#ifndef BT_AUDIO_DEMO_DEVICE_NAME
#define BT_AUDIO_DEMO_DEVICE_NAME "The Fly HFP Demo"
#endif

namespace
{
constexpr const char* TAG = "bt_audio_demo";

constexpr i2s_port_t kI2sPort            = I2S_NUM_0;
constexpr uint32_t   kHfpCvsdSampleRate  = 8000;
constexpr uint32_t   kHfpMsbcSampleRate  = 16000;
constexpr int        kCore2SpkBclk       = 12;
constexpr int        kCore2SpkLrck       = 0;
constexpr int        kCore2SpkDout       = 2;
constexpr int        kCore2MicClk        = 0;
constexpr int        kCore2MicDin        = 34;
constexpr size_t     kDmaBufferCount     = 8;
constexpr size_t     kHfpDmaFrameSamples = 120;

enum class DemoMode
{
    hostToSpeaker,
    micToHost,
};

enum class HfpCodecMode
{
    cvsd,
    msbc,
};

enum class IncomingAudioFormat
{
    none,
    cvsd_8khz,
    msbc_16khz,
};

DemoMode            g_mode                  = DemoMode::hostToSpeaker;
esp_bd_addr_t       g_host_addr             = {};
bool                g_bt_ready              = false;
bool                g_hfp_ready             = false;
bool                g_audio_ready           = false;
uint32_t            g_active_sample_rate    = kHfpMsbcSampleRate;
HfpCodecMode        g_codec_mode            = HfpCodecMode::msbc;
IncomingAudioFormat g_incoming_audio_format = IncomingAudioFormat::none;
sbc_t               g_sbc_decoder           = {};
sbc_t               g_sbc_encoder           = {};
bool                g_sbc_decoder_ready     = false;
bool                g_sbc_encoder_ready     = false;
i2s_chan_handle_t   g_i2s_tx                = nullptr;
i2s_chan_handle_t   g_i2s_rx                = nullptr;

constexpr size_t kSbcScratchBytes = 512;
uint8_t          g_sbc_decode_pcm[kSbcScratchBytes];
uint8_t          g_sbc_encode_pcm[kSbcScratchBytes];

void indicate_incoming_audio_format(IncomingAudioFormat format)
{
    g_incoming_audio_format = format;

    switch (format)
    {
    case IncomingAudioFormat::cvsd_8khz:
        ESP_LOGI(TAG, "incoming HFP audio format: CVSD/narrowband, 8 kHz; local endpoint: "
                      "signed 16-bit mono PCM");
        break;

    case IncomingAudioFormat::msbc_16khz:
        ESP_LOGI(TAG, "incoming HFP audio format: mSBC/wideband, 16 kHz; decoded local "
                      "endpoint: signed 16-bit mono PCM");
        break;

    case IncomingAudioFormat::none:
    default:
        ESP_LOGI(TAG, "incoming HFP audio format: none");
        break;
    }
}

void stop_i2s()
{
    if (g_i2s_tx)
    {
        i2s_channel_disable(g_i2s_tx);
        i2s_del_channel(g_i2s_tx);
        g_i2s_tx = nullptr;
    }

    if (g_i2s_rx)
    {
        i2s_channel_disable(g_i2s_rx);
        i2s_del_channel(g_i2s_rx);
        g_i2s_rx = nullptr;
    }
}

void finish_sbc()
{
    if (g_sbc_decoder_ready)
    {
        sbc_finish(&g_sbc_decoder);
        g_sbc_decoder_ready = false;
    }
    if (g_sbc_encoder_ready)
    {
        sbc_finish(&g_sbc_encoder);
        g_sbc_encoder_ready = false;
    }
}

bool configure_sbc_struct(sbc_t& sbc)
{
    memset(&sbc, 0, sizeof(sbc));
    if (sbc_init(&sbc, 0) != 0)
    {
        return false;
    }

    sbc.frequency  = SBC_FREQ_16000;
    sbc.mode       = SBC_MODE_MONO;
    sbc.allocation = SBC_AM_LOUDNESS;
    sbc.subbands   = SBC_SB_8;
    sbc.blocks     = SBC_BLK_15;
    sbc.bitpool    = 26;
    sbc.endian     = SBC_LE;
    return true;
}

bool init_sbc_for_msbc()
{
    finish_sbc();

    g_sbc_decoder_ready = configure_sbc_struct(g_sbc_decoder);
    g_sbc_encoder_ready = configure_sbc_struct(g_sbc_encoder);

    if (!g_sbc_decoder_ready || !g_sbc_encoder_ready)
    {
        ESP_LOGE(TAG, "libsbc initialization failed");
        finish_sbc();
        return false;
    }

    ESP_LOGI(TAG, "libsbc initialized: frame=%u bytes, pcm=%u bytes", static_cast<unsigned>(sbc_get_frame_length(&g_sbc_encoder)), static_cast<unsigned>(sbc_get_codesize(&g_sbc_encoder)));
    return true;
}

bool init_speaker_i2s(uint32_t sample_rate)
{
    stop_i2s();

    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    chan_config.dma_desc_num      = kDmaBufferCount;
    chan_config.dma_frame_num     = kHfpDmaFrameSamples;
    chan_config.auto_clear        = true;

    i2s_std_config_t config   = {};
    config.clk_cfg            = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    config.slot_cfg           = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    config.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    config.gpio_cfg.mclk      = I2S_GPIO_UNUSED;
    config.gpio_cfg.bclk      = static_cast<gpio_num_t>(kCore2SpkBclk);
    config.gpio_cfg.ws        = static_cast<gpio_num_t>(kCore2SpkLrck);
    config.gpio_cfg.dout      = static_cast<gpio_num_t>(kCore2SpkDout);
    config.gpio_cfg.din       = I2S_GPIO_UNUSED;

    if (!ok(i2s_new_channel(&chan_config, &g_i2s_tx, nullptr), "speaker i2s channel") || !ok(i2s_channel_init_std_mode(g_i2s_tx, &config), "speaker i2s std init"))
    {
        stop_i2s();
        return false;
    }

    int16_t zeros[kHfpDmaFrameSamples] = {};
    size_t  loaded                     = 0;
    i2s_channel_preload_data(g_i2s_tx, zeros, sizeof(zeros), &loaded);

    if (!ok(i2s_channel_enable(g_i2s_tx), "speaker i2s enable"))
    {
        stop_i2s();
        return false;
    }

    return true;
}

bool init_mic_pdm(uint32_t sample_rate)
{
    stop_i2s();

    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    chan_config.dma_desc_num      = kDmaBufferCount;
    chan_config.dma_frame_num     = kHfpDmaFrameSamples;

    i2s_pdm_rx_config_t config = {};
    config.clk_cfg             = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate);
    config.slot_cfg            = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    config.slot_cfg.slot_mask  = I2S_PDM_SLOT_RIGHT;
    config.gpio_cfg.clk        = static_cast<gpio_num_t>(kCore2MicClk);
    config.gpio_cfg.din        = static_cast<gpio_num_t>(kCore2MicDin);

    if (!ok(i2s_new_channel(&chan_config, nullptr, &g_i2s_rx), "mic pdm channel") || !ok(i2s_channel_init_pdm_rx_mode(g_i2s_rx, &config), "mic pdm init") || !ok(i2s_channel_enable(g_i2s_rx), "mic pdm enable"))
    {
        stop_i2s();
        return false;
    }

    return true;
}

bool configure_local_audio_for_hfp(uint32_t sample_rate)
{
    g_active_sample_rate = sample_rate;

    if (g_mode == DemoMode::hostToSpeaker)
    {
        return init_speaker_i2s(sample_rate);
    }

    return init_mic_pdm(sample_rate);
}

// HFP Client audio data callbacks:
// - Espressif docs:
//   https://docs.espressif.com/projects/esp-idf/en/v4.4.2/esp32/api-reference/bluetooth/esp_hf_client.html
// - Local SDK header:
//   esp_hf_client_api.h, esp_hf_client_register_data_callback().
//
// The callbacks are only used because init_bluetooth() selects SCO-over-HCI with
// esp_bredr_sco_datapath_set(ESP_SCO_DATA_PATH_HCI). Espressif describes buf as
// the payload of an HCI synchronous data packet. This demo uses the profile audio
// state to configure the local endpoint format:
// - ESP_HF_CLIENT_AUDIO_STATE_CONNECTED: signed 16-bit little-endian mono PCM
//   at 8000 samples/sec, matching CVSD/narrowband HFP. This path does not use
//   libsbc.
// - ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC: signed 16-bit little-endian mono
//   PCM at 16000 samples/sec, matching mSBC/wideband HFP. The added libsbc
//   dependency exposes SBC_FREQ_16000 as its lowest sample-rate mode. This demo
//   initializes libsbc only in this state. The current library header exposes
//   generic SBC settings, not an explicit HFP mSBC/H2 mode, so the conversion is
//   intentionally isolated here for later replacement if the transport framing
//   needs stricter HFP mSBC handling.
//
// esp_hf_client_init() has no sample-rate argument; the HFP/SCO codec is
// negotiated by Bluedroid and reported through ESP_HF_CLIENT_AUDIO_STATE_EVT.
// The sample-rate constants configure only the local NS4168/SPM1423 I2S/PDM
// endpoint so it matches the negotiated HFP audio path.
void hfp_incoming_audio(const uint8_t* buf, uint32_t len)
{
    if (g_mode != DemoMode::hostToSpeaker || !g_audio_ready || !buf || len == 0)
    {
        return;
    }

    // Host/Audio Gateway -> ESP32 HFP Client -> NS4168 I2S speaker.
    // Non-blocking write: dropping late audio is preferable to blocking the BT stack callback.
    if (g_codec_mode == HfpCodecMode::msbc)
    {
        if (!g_sbc_decoder_ready)
        {
            return;
        }

        size_t pos = 0;
        while (pos < len)
        {
            size_t        pcm_written = 0;
            const ssize_t consumed    = sbc_decode(&g_sbc_decoder, buf + pos, len - pos, g_sbc_decode_pcm, sizeof(g_sbc_decode_pcm), &pcm_written);
            if (consumed <= 0 || pcm_written == 0)
            {
                break;
            }

            size_t i2s_written = 0;
            i2s_channel_write(g_i2s_tx, g_sbc_decode_pcm, pcm_written, &i2s_written, 0);
            pos += static_cast<size_t>(consumed);
        }
        return;
    }

    size_t written = 0;
    i2s_channel_write(g_i2s_tx, buf, len, &written, 0);
}

uint32_t hfp_outgoing_audio(uint8_t* buf, uint32_t len)
{
    if (g_mode != DemoMode::micToHost || !g_audio_ready || !buf || len == 0)
    {
        return 0;
    }

    // SPM1423 PDM mic -> ESP32 HFP Client -> Host/Audio Gateway.
    // The HFP stack calls this after esp_hf_client_outgoing_data_ready(); it expects
    // us to return immediately with the number of bytes placed in buf, or 0 if none.
    if (g_codec_mode == HfpCodecMode::msbc)
    {
        if (!g_sbc_encoder_ready)
        {
            return 0;
        }

        const size_t codesize = sbc_get_codesize(&g_sbc_encoder);
        if (codesize == 0 || codesize > sizeof(g_sbc_encode_pcm))
        {
            return 0;
        }

        size_t read = 0;
        if (i2s_channel_read(g_i2s_rx, g_sbc_encode_pcm, codesize, &read, 0) != ESP_OK || read < codesize)
        {
            return 0;
        }

        ssize_t       encoded  = 0;
        const ssize_t consumed = sbc_encode(&g_sbc_encoder, g_sbc_encode_pcm, codesize, buf, len, &encoded);
        if (consumed <= 0 || encoded <= 0)
        {
            return 0;
        }

        return static_cast<uint32_t>(encoded);
    }

    size_t read = 0;
    if (i2s_channel_read(g_i2s_rx, buf, len, &read, 0) != ESP_OK)
    {
        return 0;
    }
    return static_cast<uint32_t>(read);
}

void tx_pump_task(void*)
{
    for (;;)
    {
        if (g_mode == DemoMode::micToHost && g_audio_ready)
        {
            esp_hf_client_outgoing_data_ready();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
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
        // RFCOMM/service-level HFP connection state changed.
        if (param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED)
        {
            esp_hf_client_connect_audio(g_host_addr);
        }
        break;

    case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        // SCO/eSCO audio link state changed; this is where the negotiated audio path is known.
        g_audio_ready = false;

        if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED)
        {
            finish_sbc();
            g_codec_mode = HfpCodecMode::cvsd;
            indicate_incoming_audio_format(IncomingAudioFormat::cvsd_8khz);
            g_audio_ready = configure_local_audio_for_hfp(kHfpCvsdSampleRate);
            ESP_LOGI(TAG, "HFP CVSD/narrowband audio connected at %lu Hz", static_cast<unsigned long>(g_active_sample_rate));
        }
        else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC)
        {
            g_codec_mode = HfpCodecMode::msbc;
            indicate_incoming_audio_format(IncomingAudioFormat::msbc_16khz);
            g_audio_ready = init_sbc_for_msbc() && configure_local_audio_for_hfp(kHfpMsbcSampleRate);
            ESP_LOGI(TAG, "HFP mSBC/wideband audio connected at %lu Hz", static_cast<unsigned long>(g_active_sample_rate));
        }
        else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED)
        {
            indicate_incoming_audio_format(IncomingAudioFormat::none);
            finish_sbc();
            stop_i2s();
            g_active_sample_rate = kHfpMsbcSampleRate;
            ESP_LOGI(TAG, "HFP audio disconnected");
        }
        break;

    case ESP_HF_CLIENT_BVRA_EVT:
        // Audio Gateway voice-recognition activation state changed.
        break;

    case ESP_HF_CLIENT_CIND_CALL_EVT:
        // Standard +CIND call indicator changed: whether a call is currently active.
        break;

    case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT:
        // Standard +CIND call-setup indicator changed: incoming/outgoing/ringing setup state.
        break;

    case ESP_HF_CLIENT_CIND_CALL_HELD_EVT:
        // Standard +CIND held-call indicator changed: whether calls are held and/or active.
        break;

    case ESP_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT:
        // Standard +CIND service indicator changed: cellular/network service availability.
        break;

    case ESP_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT:
        // Standard +CIND signal indicator changed: Audio Gateway signal strength, usually 0-5.
        break;

    case ESP_HF_CLIENT_CIND_ROAMING_STATUS_EVT:
        // Standard +CIND roaming indicator changed: whether the Audio Gateway is roaming.
        break;

    case ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT:
        // Standard +CIND battery indicator changed: Audio Gateway battery level, usually 0-5.
        break;

    case ESP_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT:
        // Current network operator name response from the Audio Gateway.
        break;

    case ESP_HF_CLIENT_BTRH_EVT:
        // Bluetooth response-and-hold state changed for an incoming call.
        break;

    case ESP_HF_CLIENT_CLIP_EVT:
        // Calling Line Identification notification for an incoming call.
        break;

    case ESP_HF_CLIENT_CCWA_EVT:
        // Call-waiting notification while another call is already active.
        break;

    case ESP_HF_CLIENT_CLCC_EVT:
        // Current-calls list entry returned by the Audio Gateway.
        break;

    case ESP_HF_CLIENT_VOLUME_CONTROL_EVT:
        // Audio Gateway requested speaker or microphone gain change via +VGS/+VGM.
        break;

    case ESP_HF_CLIENT_AT_RESPONSE_EVT:
        // Generic AT command response or error from the Audio Gateway.
        break;

    case ESP_HF_CLIENT_CNUM_EVT:
        // Subscriber number information response from the Audio Gateway.
        break;

    case ESP_HF_CLIENT_BSIR_EVT:
        // In-band ringtone setting changed: AG may send ring audio over SCO.
        break;

    case ESP_HF_CLIENT_BINP_EVT:
        // Last voice-tag phone number response from the Audio Gateway.
        break;

    case ESP_HF_CLIENT_RING_IND_EVT:
        // RING indication for an incoming call.
        break;

    case ESP_HF_CLIENT_PKT_STAT_NUMS_GET_EVT:
        // SCO/eSCO packet statistics response from esp_hf_client_pkt_stat_nums_get().
        break;

    default:
        // Unknown future SDK event.
        break;
    }
}

bool init_m5_audio_shell(DemoMode mode)
{
    auto cfg                   = M5.config();
    cfg.internal_spk           = mode == DemoMode::hostToSpeaker;
    cfg.internal_mic           = mode == DemoMode::micToHost;
    cfg.external_speaker_value = 0;
    M5.begin(cfg);

    if (mode == DemoMode::hostToSpeaker)
    {
        M5.Speaker.end();
        if (M5.Power.getType() == m5::Power_Class::pmic_t::pmic_axp192)
        {
            M5.Power.Axp192.setGPIO2(true);
        }
        return configure_local_audio_for_hfp(kHfpMsbcSampleRate);
    }

    M5.Mic.end();
    if (M5.Power.getType() == m5::Power_Class::pmic_t::pmic_axp192)
    {
        M5.Power.Axp192.setGPIO2(false);
    }
    return configure_local_audio_for_hfp(kHfpMsbcSampleRate);
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

    ok(esp_bt_sleep_disable(), "bt sleep disable");
    ok(esp_bt_gap_set_device_name(BT_AUDIO_DEMO_DEVICE_NAME), "bt name");
    ok(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE), "bt scan mode");
    ok(esp_bredr_sco_datapath_set(ESP_SCO_DATA_PATH_HCI), "sco hci path");

    g_bt_ready = true;
    return true;
}

bool start_demo(DemoMode mode, const char* host_mac)
{
    if (!parse_mac(host_mac, g_host_addr))
    {
        ESP_LOGE(TAG, "invalid BT_AUDIO_DEMO_HOST_MAC: %s", host_mac ? host_mac : "(null)");
        return false;
    }

    g_mode        = mode;
    g_audio_ready = false;

    if (!init_m5_audio_shell(mode) || !init_bluetooth())
    {
        return false;
    }

    if (!g_hfp_ready)
    {
        if (!ok(esp_hf_client_register_callback(hfp_event), "hfp callback") || !ok(esp_hf_client_register_data_callback(hfp_incoming_audio, hfp_outgoing_audio), "hfp data callback") ||
            // esp_hf_client_init() has no per-instance sample-rate or codec config.
            // It initializes the Bluedroid HFP Client profile after Bluedroid is enabled.
            // Audio format is determined by HFP/SCO negotiation and reported by
            // ESP_HF_CLIENT_AUDIO_STATE_EVT. The callback then reconfigures our local
            // I2S/PDM endpoint to either 8 kHz CVSD or 16 kHz mSBC.
            !ok(esp_hf_client_init(), "hfp init"))
        {
            return false;
        }
        g_hfp_ready = true;
        xTaskCreatePinnedToCore(tx_pump_task, "hfp_tx_pump", 2048, nullptr, 2, nullptr, 0);
    }

    return ok(esp_hf_client_connect(g_host_addr), "hfp connect");
}
} // namespace

bool bt_audio_demo_host_to_ns4168(const char* host_mac)
{
    return start_demo(DemoMode::hostToSpeaker, host_mac);
}

bool bt_audio_demo_host_to_ns4168()
{
    return bt_audio_demo_host_to_ns4168(BT_AUDIO_DEMO_HOST_MAC);
}

bool bt_audio_demo_spm1423_to_host(const char* host_mac)
{
    return start_demo(DemoMode::micToHost, host_mac);
}

bool bt_audio_demo_spm1423_to_host()
{
    return bt_audio_demo_spm1423_to_host(BT_AUDIO_DEMO_HOST_MAC);
}
