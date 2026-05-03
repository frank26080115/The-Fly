#include <Arduino.h>
#include <Wire.h>

#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "utilfuncs.h"

namespace
{
constexpr const char* TAG = "push_to_talk_demo";

constexpr i2s_port_t kI2sPort        = I2S_NUM_0;
constexpr uint32_t   kSampleRate     = 16000;
constexpr uint32_t   kModeDurationMs = 10000;
constexpr size_t     kFrameSamples   = 256;
constexpr size_t     kDmaBufferCount = 8;

constexpr int kCore2SpkBclk = 12;
constexpr int kCore2SpkLrck = 0;
constexpr int kCore2SpkDout = 2;
constexpr int kCore2MicClk  = 0;
constexpr int kCore2MicDin  = 34;

constexpr uint8_t kAxp192Address      = 0x34;
constexpr uint8_t kAxp192Gpio2Control = 0x93;
constexpr int     kInternalI2cSda     = 21;
constexpr int     kInternalI2cScl     = 22;

TaskHandle_t      g_demo_task   = nullptr;
uint32_t          g_noise_state = 0x12345678;
i2s_chan_handle_t g_i2s_tx      = nullptr;
i2s_chan_handle_t g_i2s_rx      = nullptr;

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

void set_core2_speaker_enabled(bool enabled)
{
    static bool wire_started = false;
    if (!wire_started)
    {
        Wire.begin(kInternalI2cSda, kInternalI2cScl, 400000);
        wire_started = true;
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

bool init_speaker_i2s()
{
    stop_i2s();
    set_core2_speaker_enabled(true);

    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    chan_config.dma_desc_num      = kDmaBufferCount;
    chan_config.dma_frame_num     = kFrameSamples;
    chan_config.auto_clear        = true;

    i2s_std_config_t config   = {};
    config.clk_cfg            = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
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

    int16_t zeros[kFrameSamples] = {};
    size_t  loaded               = 0;
    i2s_channel_preload_data(g_i2s_tx, zeros, sizeof(zeros), &loaded);

    if (!ok(i2s_channel_enable(g_i2s_tx), "speaker i2s enable"))
    {
        stop_i2s();
        return false;
    }

    return true;
}

bool init_mic_pdm()
{
    stop_i2s();
    set_core2_speaker_enabled(false);

    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    chan_config.dma_desc_num      = kDmaBufferCount;
    chan_config.dma_frame_num     = kFrameSamples;

    i2s_pdm_rx_config_t config = {};
    config.clk_cfg             = I2S_PDM_RX_CLK_DEFAULT_CONFIG(kSampleRate);
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

int16_t next_noise_sample()
{
    g_noise_state = (1664525UL * g_noise_state) + 1013904223UL;
    return static_cast<int16_t>((static_cast<int32_t>(g_noise_state >> 16) - 32768) / 12);
}

void run_speaker_phase()
{
    if (!init_speaker_i2s())
    {
        delay(1000);
        return;
    }

    int16_t        samples[kFrameSamples];
    const uint32_t started = millis();
    while (millis() - started < kModeDurationMs)
    {
        for (size_t i = 0; i < kFrameSamples; ++i)
        {
            samples[i] = next_noise_sample();
        }

        size_t written = 0;
        i2s_channel_write(g_i2s_tx, samples, sizeof(samples), &written, 100);
        taskYIELD();
    }
}

void run_mic_phase()
{
    if (!init_mic_pdm())
    {
        delay(1000);
        return;
    }

    int16_t        samples[kFrameSamples];
    const uint32_t started = millis();
    uint32_t       frames  = 0;
    uint32_t       peak    = 0;

    while (millis() - started < kModeDurationMs)
    {
        size_t bytes_read = 0;
        if (i2s_channel_read(g_i2s_rx, samples, sizeof(samples), &bytes_read, 100) == ESP_OK)
        {
            const size_t count = bytes_read / sizeof(samples[0]);
            for (size_t i = 0; i < count; ++i)
            {
                const int32_t  sample    = samples[i];
                const uint32_t magnitude = static_cast<uint32_t>(sample < 0 ? -sample : sample);
                if (magnitude > peak)
                {
                    peak = magnitude;
                }
            }
            ++frames;
        }
        taskYIELD();
    }

    ESP_LOGI(TAG, "mic phase read %lu buffers, peak=%lu", static_cast<unsigned long>(frames), static_cast<unsigned long>(peak));
}

void demo_task(void*)
{
    for (;;)
    {
        ESP_LOGI(TAG, "speaker phase: random PCM to NS4168");
        run_speaker_phase();

        ESP_LOGI(TAG, "mic phase: PDM samples from SPM1423");
        run_mic_phase();
    }
}
} // namespace

bool push_to_talk_demo_start()
{
    if (g_demo_task)
    {
        return true;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(demo_task, "ptt_demo", 4096, nullptr, 2, &g_demo_task, 0);

    if (created != pdPASS)
    {
        g_demo_task = nullptr;
        ESP_LOGE(TAG, "failed to create demo task");
        return false;
    }

    return true;
}

bool push_to_talk_demo()
{
    return push_to_talk_demo_start();
}
