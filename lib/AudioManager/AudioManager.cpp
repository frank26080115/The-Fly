#include "AudioManager.h"

#include <M5Unified.h>
#include <Wire.h>

#include "AudioFileRecorder.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "utilfuncs.h"

namespace AudioManager
{
namespace
{

constexpr const char* TAG = "AudioManager";

constexpr i2s_port_t kI2sPort = I2S_NUM_0;

constexpr int kCore2SpkBclk = 12;
constexpr int kCore2SpkLrck = 0;
constexpr int kCore2SpkDout = 2;
constexpr int kCore2MicClk  = 0;
constexpr int kCore2MicDin  = 34;

constexpr uint8_t kAxp192Address      = 0x34;
constexpr uint8_t kAxp192Gpio2Control = 0x93;
constexpr int     kInternalI2cSda     = 21;
constexpr int     kInternalI2cScl     = 22;

constexpr size_t     kFifoCapacitySamples  = 8192;
constexpr size_t     kFifoWatermarkSamples = 240;
constexpr size_t     kPumpSamples          = 240;
constexpr size_t     kDmaBufferCount       = 8;
constexpr TickType_t kPumpTaskDelay        = pdMS_TO_TICKS(2);

AudioFifo g_bt_to_speaker(kFifoCapacitySamples, kFifoWatermarkSamples);
AudioFifo g_bt_to_file(kFifoCapacitySamples, 0);
AudioFifo g_mic_to_bt(kFifoCapacitySamples, kFifoWatermarkSamples);
AudioFifo g_mic_to_file(kFifoCapacitySamples, 0);

Hardware          g_hardware     = Hardware::Core2Internal;
I2SMode           g_mode         = I2SMode::Stopped;
TaskHandle_t      g_pump_task    = nullptr;
i2s_chan_handle_t g_i2s_tx       = nullptr;
i2s_chan_handle_t g_i2s_rx       = nullptr;
uint8_t           g_volume       = kMaxVolume;
bool              g_initialized  = false;
bool              g_wire_started = false;

int16_t g_mono_buffer[kPumpSamples];
int16_t g_stereo_buffer[kPumpSamples * 2];

void set_core2_speaker_enabled(bool enabled)
{
    if (!g_wire_started)
    {
        Wire.begin(kInternalI2cSda, kInternalI2cScl, 400000);
        g_wire_started = true;
    }

    Wire.beginTransmission(kAxp192Address);
    Wire.write(kAxp192Gpio2Control);
    Wire.write(enabled ? 0x06 : 0x05);
    const uint8_t result = Wire.endTransmission();
    if (result != 0)
    {
        ESP_LOGW(TAG, "AXP192 speaker GPIO2 write failed: %u", result);
    }
}

int16_t apply_volume(int16_t sample)
{
    return static_cast<int16_t>((static_cast<int32_t>(sample) * g_volume) / kMaxVolume);
}

void mono_to_stereo_with_volume(const int16_t* src, int16_t* dst, size_t frames)
{
    for (size_t i = 0; i < frames; ++i)
    {
        const int16_t sample = apply_volume(src[i]);
        dst[i * 2]           = sample;
        dst[i * 2 + 1]       = sample;
    }
}

bool IRAM_ATTR i2s_transfer_ready(i2s_chan_handle_t, i2s_event_data_t*, void*)
{
    if (!g_pump_task)
    {
        return false;
    }

    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(g_pump_task, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
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

    g_mode = I2SMode::Stopped;
    if (g_hardware == Hardware::Core2Internal)
    {
        set_core2_speaker_enabled(false);
    }
}

bool register_i2s_callbacks(i2s_chan_handle_t handle, bool rx)
{
    i2s_event_callbacks_t callbacks = {};
    if (rx)
    {
        callbacks.on_recv = i2s_transfer_ready;
    }
    else
    {
        callbacks.on_sent = i2s_transfer_ready;
    }

    return ok(i2s_channel_register_event_callback(handle, &callbacks, nullptr), "i2s event callback");
}

bool preload_silence(i2s_chan_handle_t handle)
{
    int16_t zeros[kPumpSamples * 2] = {};
    size_t  loaded                  = 0;
    return ok(i2s_channel_preload_data(handle, zeros, sizeof(zeros), &loaded), "speaker i2s preload");
}

bool enable_channel(i2s_chan_handle_t handle, const char* label)
{
    if (!ok(i2s_channel_enable(handle), label))
    {
        stop_i2s();
        return false;
    }
    return true;
}

bool enable_core2_speaker()
{
    stop_i2s();
    set_core2_speaker_enabled(true);

    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    chan_config.dma_desc_num      = kDmaBufferCount;
    chan_config.dma_frame_num     = kPumpSamples;
    chan_config.auto_clear        = true;

    i2s_std_config_t config = {};
    config.clk_cfg          = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRateHz);
    config.slot_cfg         = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    config.gpio_cfg.mclk    = I2S_GPIO_UNUSED;
    config.gpio_cfg.bclk    = static_cast<gpio_num_t>(kCore2SpkBclk);
    config.gpio_cfg.ws      = static_cast<gpio_num_t>(kCore2SpkLrck);
    config.gpio_cfg.dout    = static_cast<gpio_num_t>(kCore2SpkDout);
    config.gpio_cfg.din     = I2S_GPIO_UNUSED;

    if (!ok(i2s_new_channel(&chan_config, &g_i2s_tx, nullptr), "speaker i2s channel") || !ok(i2s_channel_init_std_mode(g_i2s_tx, &config), "speaker i2s std init") || !register_i2s_callbacks(g_i2s_tx, false) || !preload_silence(g_i2s_tx) || !enable_channel(g_i2s_tx, "speaker i2s enable"))
    {
        stop_i2s();
        return false;
    }

    g_mode = I2SMode::Speaker;
    return true;
}

bool enable_core2_mic()
{
    stop_i2s();
    set_core2_speaker_enabled(false);

    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    chan_config.dma_desc_num      = kDmaBufferCount;
    chan_config.dma_frame_num     = kPumpSamples;

    i2s_pdm_rx_config_t config = {};
    config.clk_cfg             = I2S_PDM_RX_CLK_DEFAULT_CONFIG(kSampleRateHz);
    config.slot_cfg            = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    config.slot_cfg.slot_mask  = I2S_PDM_SLOT_RIGHT;
    config.gpio_cfg.clk        = static_cast<gpio_num_t>(kCore2MicClk);
    config.gpio_cfg.din        = static_cast<gpio_num_t>(kCore2MicDin);

    if (!ok(i2s_new_channel(&chan_config, nullptr, &g_i2s_rx), "mic pdm channel") || !ok(i2s_channel_init_pdm_rx_mode(g_i2s_rx, &config), "mic pdm init") || !register_i2s_callbacks(g_i2s_rx, true) || !enable_channel(g_i2s_rx, "mic pdm enable"))
    {
        stop_i2s();
        return false;
    }

    g_mode = I2SMode::Mic;
    return true;
}

bool enable_wm8960_speaker()
{
    ESP_LOGE(TAG, "WM8960 I2S pin/codec setup is not implemented yet");
    return false;
}

bool enable_wm8960_mic()
{
    ESP_LOGE(TAG, "WM8960 I2S pin/codec setup is not implemented yet");
    return false;
}

void pump_task(void*)
{
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, kPumpTaskDelay);

        pump_bt2s();
        pump_mic2bt();
    }
}

} // namespace

bool init(Hardware hardware)
{
    g_hardware = hardware;

    auto cfg                   = M5.config();
    cfg.internal_spk           = hardware == Hardware::Core2Internal;
    cfg.internal_mic           = hardware == Hardware::Core2Internal;
    cfg.external_speaker_value = 0;
    M5.begin(cfg);
    M5.Speaker.end();
    M5.Mic.end();

    g_bt_to_speaker.clear();
    g_bt_to_file.clear();
    g_mic_to_bt.clear();
    g_mic_to_file.clear();
    g_mic_to_bt.setMuted(false);
    g_mic_to_file.setMuted(false);

    AudioFileRecorder::init(g_bt_to_file, g_mic_to_file);

    g_initialized = true;
    return true;
}

void stop()
{
    stop_i2s();
}

bool enableSpeakerMode()
{
    if (!g_initialized)
    {
        init(g_hardware);
    }

    return g_hardware == Hardware::Core2Internal ? enable_core2_speaker() : enable_wm8960_speaker();
}

bool enableMicMode()
{
    if (!g_initialized)
    {
        init(g_hardware);
    }

    return g_hardware == Hardware::Core2Internal ? enable_core2_mic() : enable_wm8960_mic();
}

I2SMode mode()
{
    return g_mode;
}

void pump_bt2s()
{
    if (g_mode != I2SMode::Speaker || !g_i2s_tx || !g_bt_to_speaker.readyToDequeue())
    {
        return;
    }

    const size_t frames = g_bt_to_speaker.dequeueMono(g_mono_buffer, kPumpSamples);
    if (frames == 0)
    {
        return;
    }

    mono_to_stereo_with_volume(g_mono_buffer, g_stereo_buffer, frames);

    size_t written = 0;
    i2s_channel_write(g_i2s_tx, g_stereo_buffer, frames * 2 * sizeof(int16_t), &written, 0);
}

void pump_mic2bt()
{
    if (g_mode != I2SMode::Mic || !g_i2s_rx || g_mic_to_bt.availableToWrite() == 0)
    {
        return;
    }

    size_t bytes_read = 0;
    if (i2s_channel_read(g_i2s_rx, g_mono_buffer, sizeof(g_mono_buffer), &bytes_read, 0) != ESP_OK || bytes_read == 0)
    {
        return;
    }

    const size_t samples = bytes_read / sizeof(g_mono_buffer[0]);
    if (samples == 0)
    {
        return;
    }

    g_mic_to_bt.queue(g_mono_buffer, samples, kSampleRateHz);
    g_mic_to_file.queue(g_mono_buffer, samples, kSampleRateHz);
}

bool startPumpTask(BaseType_t coreId, UBaseType_t priority)
{
    if (g_pump_task)
    {
        return true;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(pump_task, "audio_pump", 4096, nullptr, priority, &g_pump_task, coreId);
    if (created != pdPASS)
    {
        g_pump_task = nullptr;
        ESP_LOGE(TAG, "failed to create audio pump task");
        return false;
    }

    return true;
}

void hfp_incoming_audio(const uint8_t* buf, uint32_t len)
{
    if (!buf || len < sizeof(int16_t))
    {
        return;
    }

    const size_t samples = len / sizeof(int16_t);
    const auto*  pcm     = reinterpret_cast<const int16_t*>(buf);
    g_bt_to_speaker.queue(pcm, samples, kSampleRateHz);
    g_bt_to_file.queue(pcm, samples, kSampleRateHz);
    pump_bt2s();
}

uint32_t hfp_outgoing_audio(uint8_t* buf, uint32_t len)
{
    if (!buf || len < sizeof(int16_t))
    {
        return 0;
    }

    pump_mic2bt();

    const size_t max_samples = len / sizeof(int16_t);
    const size_t read        = g_mic_to_bt.dequeueMono(reinterpret_cast<int16_t*>(buf), max_samples);

    pump_mic2bt();
    return static_cast<uint32_t>(read * sizeof(int16_t));
}

AudioFifo& bluetoothToSpeakerFifo()
{
    return g_bt_to_speaker;
}

AudioFifo& bluetoothToFileFifo()
{
    return g_bt_to_file;
}

AudioFifo& micToBluetoothFifo()
{
    return g_mic_to_bt;
}

AudioFifo& micToFileFifo()
{
    return g_mic_to_file;
}

void setVolume(uint8_t volume)
{
    g_volume = volume > kMaxVolume ? kMaxVolume : volume;
}

void volumeUp()
{
    if (g_volume < kMaxVolume)
    {
        ++g_volume;
    }
}

void volumeDown()
{
    if (g_volume > kMinVolume)
    {
        --g_volume;
    }
}

uint8_t volume()
{
    return g_volume;
}

void setMicMuted(bool muted)
{
    g_mic_to_bt.setMuted(muted);
    g_mic_to_file.setMuted(muted);
}

void muteMic()
{
    setMicMuted(true);
}

void unmuteMic()
{
    setMicMuted(false);
}

bool micMuted()
{
    return g_mic_to_bt.muted();
}

} // namespace AudioManager
