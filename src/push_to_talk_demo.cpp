#include <Arduino.h>
#include <Wire.h>

#include "driver/i2s.h"
#include "esp_log.h"
#include "utilfuncs.h"

namespace
{
constexpr const char* TAG = "push_to_talk_demo";

constexpr i2s_port_t kI2sPort        = I2S_NUM_0;
constexpr uint32_t   kSampleRate     = 16000;
constexpr uint32_t   kModeDurationMs = 10000;
constexpr size_t     kFrameSamples   = 256;

constexpr int kCore2SpkBclk = 12;
constexpr int kCore2SpkLrck = 0;
constexpr int kCore2SpkDout = 2;
constexpr int kCore2MicClk  = 0;
constexpr int kCore2MicDin  = 34;

constexpr uint8_t kAxp192Address      = 0x34;
constexpr uint8_t kAxp192Gpio2Control = 0x93;
constexpr int     kInternalI2cSda     = 21;
constexpr int     kInternalI2cScl     = 22;

TaskHandle_t g_demo_task   = nullptr;
uint32_t     g_noise_state = 0x12345678;

void stop_i2s()
{
    i2s_stop(kI2sPort);
    i2s_driver_uninstall(kI2sPort);
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

    i2s_config_t config         = {};
    config.mode                 = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate          = kSampleRate;
    config.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count        = 8;
    config.dma_buf_len          = kFrameSamples;
    config.use_apll             = false;
    config.tx_desc_auto_clear   = true;
    config.fixed_mclk           = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num       = kCore2SpkBclk;
    pins.ws_io_num        = kCore2SpkLrck;
    pins.data_out_num     = kCore2SpkDout;
    pins.data_in_num      = I2S_PIN_NO_CHANGE;

    return ok(i2s_driver_install(kI2sPort, &config, 0, nullptr), "speaker i2s install") && ok(i2s_set_pin(kI2sPort, &pins), "speaker i2s pins") && ok(i2s_zero_dma_buffer(kI2sPort), "speaker i2s zero") && ok(i2s_start(kI2sPort), "speaker i2s start");
}

bool init_mic_pdm()
{
    stop_i2s();
    set_core2_speaker_enabled(false);

    i2s_config_t config         = {};
    config.mode                 = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    config.sample_rate          = kSampleRate;
    config.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT;
    config.communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT;
    config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count        = 8;
    config.dma_buf_len          = kFrameSamples;
    config.use_apll             = false;
    config.tx_desc_auto_clear   = false;
    config.fixed_mclk           = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num       = I2S_PIN_NO_CHANGE;
    pins.ws_io_num        = kCore2MicClk;
    pins.data_out_num     = I2S_PIN_NO_CHANGE;
    pins.data_in_num      = kCore2MicDin;

    return ok(i2s_driver_install(kI2sPort, &config, 0, nullptr), "mic pdm install") && ok(i2s_set_pin(kI2sPort, &pins), "mic pdm pins") && ok(i2s_start(kI2sPort), "mic pdm start");
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
        i2s_write(kI2sPort, samples, sizeof(samples), &written, pdMS_TO_TICKS(100));
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
        if (i2s_read(kI2sPort, samples, sizeof(samples), &bytes_read, pdMS_TO_TICKS(100)) == ESP_OK)
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
