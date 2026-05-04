#include <Arduino.h>
#include <M5Unified.h>

#include "driver/i2s_pdm.h"
#include "esp_log.h"

#include "AudioFileRecorder.h"
#include "ClockAgent.h"
#include "SdCard.h"
#include "conf.h"

namespace
{
constexpr const char* TAG = "mic_sd_recorder_demo";

constexpr i2s_port_t kI2sPort           = I2S_NUM_0;
constexpr uint32_t   kSampleRate        = 16000;
constexpr uint32_t   kRecordDurationMs  = 60000;
constexpr size_t     kDmaFrameSamples   = 1024;
constexpr size_t     kDmaBufferCount    = 8;
constexpr size_t     kRecordBufferBytes = 8192;

constexpr int kSPM1423MicClk = 0;
constexpr int kSPM1423MicDin = 34;

i2s_chan_handle_t g_i2s_rx = nullptr;

void stop_mic_pdm()
{
    if (!g_i2s_rx)
    {
        return;
    }

    i2s_channel_disable(g_i2s_rx);
    i2s_del_channel(g_i2s_rx);
    g_i2s_rx = nullptr;
}

bool init_m5()
{
    auto cfg                   = M5.config();
    cfg.serial_baudrate        = 115200;
    cfg.internal_mic           = false;
    cfg.internal_spk           = false;
    cfg.external_speaker_value = 0;
    M5.begin(cfg);
    Clock.begin();
    M5.Mic.end();
    M5.Speaker.end();
    if (M5.Power.getType() == m5::Power_Class::pmic_t::pmic_axp192)
    {
        M5.Power.Axp192.setGPIO2(false);
    }
    return true;
}

bool init_sd()
{
    return SdCard::begin();
}

void make_recording_path(char* sd_path, size_t sd_path_size)
{
    m5::rtc_datetime_t now = Clock.getDateTime();
    snprintf(sd_path, sd_path_size, "/M-%04d-%02d-%02d-%02d-%02d-%02d-U.raw", now.date.year, now.date.month, now.date.date, now.time.hours, now.time.minutes, now.time.seconds);
}

uint64_t max_prealloc_size()
{
    const uint64_t free_bytes = SdCard::freeBytes();
    uint64_t       size       = (free_bytes / kHalfGiB) * kHalfGiB;

    if (size > 7ULL * kHalfGiB)
    {
        size = 7ULL * kHalfGiB;
    }
    return size;
}

bool init_mic_pdm()
{
    stop_mic_pdm();

    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    chan_config.dma_desc_num      = kDmaBufferCount;
    chan_config.dma_frame_num     = kDmaFrameSamples;

    i2s_pdm_rx_config_t config = {};
    config.clk_cfg             = I2S_PDM_RX_CLK_DEFAULT_CONFIG(kSampleRate);
    config.slot_cfg            = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    config.slot_cfg.slot_mask  = I2S_PDM_SLOT_RIGHT;
    config.gpio_cfg.clk        = static_cast<gpio_num_t>(kSPM1423MicClk);
    config.gpio_cfg.din        = static_cast<gpio_num_t>(kSPM1423MicDin);

    esp_err_t err = i2s_new_channel(&chan_config, nullptr, &g_i2s_rx);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "mic pdm channel failed: %s", esp_err_to_name(err));
        return false;
    }

    err = i2s_channel_init_pdm_rx_mode(g_i2s_rx, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "mic pdm init failed: %s", esp_err_to_name(err));
        stop_mic_pdm();
        return false;
    }

    err = i2s_channel_enable(g_i2s_rx);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "mic pdm enable failed: %s", esp_err_to_name(err));
        stop_mic_pdm();
        return false;
    }

    return true;
}

uint32_t record_audio(FsFile& file)
{
    static uint8_t buffer[kRecordBufferBytes];
    uint32_t       written_total = 0;
    const uint32_t started       = millis();

    while (millis() - started < kRecordDurationMs)
    {
        size_t          bytes_read = 0;
        const esp_err_t err        = i2s_channel_read(g_i2s_rx, buffer, sizeof(buffer), &bytes_read, 250);
        if (err == ESP_OK && bytes_read > 0)
        {
            written_total += file.write(buffer, bytes_read);
        }
        taskYIELD();
    }

    file.flush();
    ESP_LOGI(TAG, "recorded %lu bytes", static_cast<unsigned long>(written_total));
    return written_total;
}
} // namespace

bool mic_sd_recorder_demo()
{
    init_m5();
    if (!init_sd())
    {
        return false;
    }

    char sd_path[48];
    make_recording_path(sd_path, sizeof(sd_path));

    FsFile file;
    if (!file.open(sd_path, O_RDWR | O_CREAT | O_TRUNC))
    {
        ESP_LOGE(TAG, "open failed: %s", sd_path);
        return false;
    }

    AudioFileRecorder::grow_file(file, max_prealloc_size());

    if (!init_mic_pdm())
    {
        file.close();
        return false;
    }

    const uint32_t actual_size = record_audio(file);
    stop_mic_pdm();

    if (!file.truncate(actual_size))
    {
        ESP_LOGE(TAG, "truncate failed");
        file.close();
        return false;
    }

    file.close();
    ESP_LOGI(TAG, "closed %s", sd_path);
    return true;
}
