#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <M5Unified.h>
#include <errno.h>
#include <unistd.h>

#include "driver/i2s.h"
#include "esp_log.h"

namespace
{
constexpr const char *TAG = "mic_sd_recorder_demo";

constexpr i2s_port_t kI2sPort = I2S_NUM_0;
constexpr uint32_t kSampleRate = 16000;
constexpr uint32_t kRecordDurationMs = 60000;
constexpr size_t kDmaFrameSamples = 1024;
constexpr size_t kRecordBufferBytes = 8192;
constexpr uint64_t kHalfGiB = 512ULL * 1024ULL * 1024ULL;

constexpr int kCore2SdSclk = 18;
constexpr int kCore2SdMiso = 38;
constexpr int kCore2SdMosi = 23;
constexpr int kCore2SdCs = 4;
constexpr int kCore2MicClk = 0;
constexpr int kCore2MicDin = 34;

bool init_m5()
{
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    cfg.internal_mic = false;
    cfg.internal_spk = false;
    cfg.external_speaker_value = 0;
    M5.begin(cfg);
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
    SPI.begin(kCore2SdSclk, kCore2SdMiso, kCore2SdMosi, kCore2SdCs);
    if (!SD.begin(kCore2SdCs, SPI, 40000000U))
    {
        ESP_LOGE(TAG, "microSD init failed");
        return false;
    }

    const uint64_t total = SD.totalBytes();
    const uint64_t used = SD.usedBytes();
    ESP_LOGI(TAG, "microSD space: total=%llu used=%llu free=%llu",
             static_cast<unsigned long long>(total),
             static_cast<unsigned long long>(used),
             static_cast<unsigned long long>(total > used ? total - used : 0));
    return true;
}

void make_recording_path(char *sd_path, size_t sd_path_size, char *vfs_path, size_t vfs_path_size)
{
    m5::rtc_datetime_t now = M5.Rtc.getDateTime();
    snprintf(sd_path, sd_path_size,
             "/M-%04d-%02d-%02d-%02d-%02d-%02d-U.raw",
             now.date.year,
             now.date.month,
             now.date.date,
             now.time.hours,
             now.time.minutes,
             now.time.seconds);
    snprintf(vfs_path, vfs_path_size, "/sd%s", sd_path);
}

uint64_t max_prealloc_size()
{
    const uint64_t total = SD.totalBytes();
    const uint64_t used = SD.usedBytes();
    const uint64_t free_bytes = total > used ? total - used : 0;
    uint64_t size = (free_bytes / kHalfGiB) * kHalfGiB;

    if (size > 7ULL * kHalfGiB)
    {
        size = 7ULL * kHalfGiB;
    }
    return size;
}

bool grow_file(File &file, uint64_t size)
{
    while (size >= kHalfGiB)
    {
        if (file.seek(static_cast<uint32_t>(size - 1)))
        {
            if (file.write(static_cast<uint8_t>(0)) == 1)
            {
                file.flush();
                file.seek(0);
                ESP_LOGI(TAG, "reserved %llu bytes", static_cast<unsigned long long>(size));
                return true;
            }
        }

        ESP_LOGW(TAG, "reserve failed at %llu bytes", static_cast<unsigned long long>(size));
        size -= kHalfGiB;
    }

    file.seek(0);
    return false;
}

bool init_mic_pdm()
{
    i2s_stop(kI2sPort);
    i2s_driver_uninstall(kI2sPort);

    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    config.sample_rate = kSampleRate;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
    config.communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 8;
    config.dma_buf_len = kDmaFrameSamples;
    config.use_apll = false;
    config.tx_desc_auto_clear = false;
    config.fixed_mclk = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = I2S_PIN_NO_CHANGE;
    pins.ws_io_num = kCore2MicClk;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = kCore2MicDin;

    esp_err_t err = i2s_driver_install(kI2sPort, &config, 0, nullptr);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "mic pdm install failed: %s", esp_err_to_name(err));
        return false;
    }

    err = i2s_set_pin(kI2sPort, &pins);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "mic pdm pins failed: %s", esp_err_to_name(err));
        i2s_driver_uninstall(kI2sPort);
        return false;
    }

    err = i2s_start(kI2sPort);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "mic pdm start failed: %s", esp_err_to_name(err));
        i2s_driver_uninstall(kI2sPort);
        return false;
    }

    return true;
}

uint32_t record_audio(File &file)
{
    static uint8_t buffer[kRecordBufferBytes];
    uint32_t written_total = 0;
    const uint32_t started = millis();

    while (millis() - started < kRecordDurationMs)
    {
        size_t bytes_read = 0;
        const esp_err_t err = i2s_read(kI2sPort, buffer, sizeof(buffer), &bytes_read, pdMS_TO_TICKS(250));
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
}

bool mic_sd_recorder_demo()
{
    init_m5();
    if (!init_sd())
    {
        return false;
    }

    char sd_path[48];
    char vfs_path[56];
    make_recording_path(sd_path, sizeof(sd_path), vfs_path, sizeof(vfs_path));

    File file = SD.open(sd_path, FILE_WRITE);
    if (!file)
    {
        ESP_LOGE(TAG, "open failed: %s", sd_path);
        return false;
    }

    grow_file(file, max_prealloc_size());

    if (!init_mic_pdm())
    {
        file.close();
        return false;
    }

    const uint32_t actual_size = record_audio(file);
    i2s_stop(kI2sPort);
    i2s_driver_uninstall(kI2sPort);
    file.close();

    if (truncate(vfs_path, static_cast<off_t>(actual_size)) != 0)
    {
        ESP_LOGE(TAG, "truncate failed: errno=%d", errno);
        return false;
    }

    ESP_LOGI(TAG, "closed %s", sd_path);
    return true;
}
