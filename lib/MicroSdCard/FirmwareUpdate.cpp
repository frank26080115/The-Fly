#include "FirmwareUpdate.h"

#include <Arduino.h>
#include <SdFat.h>
#include <stddef.h>

#include "MicroSdCard.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace MicroSdCard
{
namespace
{

constexpr const char* TAG = "FirmwareUpdate";
constexpr const char* kFirmwareUpdatePath = "/firmware.bin";
constexpr size_t      kBufferSize = 4096;
constexpr uint32_t    kProgressCheckBytes = 4096;
constexpr uint32_t    kProgressIntervalMs = 500;
constexpr uint64_t    kMockBytesTotal = 4ULL * 1024ULL * 1024ULL;
constexpr uint32_t    kMockDurationMs = 2UL * 60UL * 1000UL;
constexpr uint64_t    kMockChunkCount = (kMockBytesTotal + kBufferSize - 1ULL) / kBufferSize;
constexpr uint32_t    kMockChunkDelayMs = static_cast<uint32_t>((kMockDurationMs + kMockChunkCount - 1ULL) / kMockChunkCount);

static_assert(kMockChunkCount * kMockChunkDelayMs >= kMockDurationMs, "mock update must last at least two minutes");

uint8_t g_buffer[kBufferSize];

void feed_watchdog()
{
    esp_task_wdt_reset();
    taskYIELD();
}

} // namespace

FirmwareUpdateResult update_firmware(FirmwareUpdateProgressCallback progressCallback)
{
    #ifndef TEST_MOCK_FW_UPDATE
    if (!isReady())
    {
        return FirmwareUpdateResult::CardNotReady;
    }

    FsFile firmware;
    if (!firmware.open(kFirmwareUpdatePath, O_RDONLY) || !firmware.isFile())
    {
        if (firmware)
        {
            firmware.close();
        }
        return FirmwareUpdateResult::FileOpenFailed;
    }

    const uint64_t bytes_total = firmware.fileSize();
    if (bytes_total == 0)
    {
        firmware.close();
        return FirmwareUpdateResult::EmptyFile;
    }

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition)
    {
        firmware.close();
        return FirmwareUpdateResult::NoUpdatePartition;
    }

    if (bytes_total > update_partition->size)
    {
        ESP_LOGE(TAG,
                 "firmware.bin too large: file=%llu partition=%lu",
                 static_cast<unsigned long long>(bytes_total),
                 static_cast<unsigned long>(update_partition->size));
        firmware.close();
        return FirmwareUpdateResult::FileTooLarge;
    }

    esp_ota_handle_t ota_handle = 0;
    feed_watchdog();
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        firmware.close();
        return FirmwareUpdateResult::OtaBeginFailed;
    }

    #elif TEST_MOCK_FW_UPDATE == -1
    return FirmwareUpdateResult::CardNotReady;
    #elif TEST_MOCK_FW_UPDATE == -2
    return FirmwareUpdateResult::FileOpenFailed;
    #elif TEST_MOCK_FW_UPDATE == -3
    return FirmwareUpdateResult::EmptyFile;
    #elif TEST_MOCK_FW_UPDATE == -4
    return FirmwareUpdateResult::NoUpdatePartition;
    #elif TEST_MOCK_FW_UPDATE == -5
    return FirmwareUpdateResult::FileTooLarge;
    #elif TEST_MOCK_FW_UPDATE == -6
    return FirmwareUpdateResult::OtaBeginFailed;
    #else // TEST_MOCK_FW_UPDATE is active
    const uint64_t bytes_total = kMockBytesTotal;
    #endif

    uint64_t bytes_written = 0;
    uint64_t last_reported_bytes = bytes_total + 1;
    uint32_t bytes_since_progress_check = 0;
    uint32_t last_progress_ms = millis();

    if (progressCallback)
    {
        progressCallback(0, bytes_total);
        last_reported_bytes = 0;
    }

    while (bytes_written < bytes_total)
    {
        const uint64_t remaining = bytes_total - bytes_written;
        const size_t   to_read = remaining < kBufferSize ? static_cast<size_t>(remaining) : kBufferSize;

        #ifndef TEST_MOCK_FW_UPDATE
        const int      read_count = firmware.read(g_buffer, to_read);
        if (read_count <= 0)
        {
            ESP_LOGE(TAG, "firmware.bin read failed after %llu bytes", static_cast<unsigned long long>(bytes_written));
            esp_ota_abort(ota_handle);
            firmware.close();
            return FirmwareUpdateResult::FileReadFailed;
        }

        err = esp_ota_write(ota_handle, g_buffer, static_cast<size_t>(read_count));
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_write failed after %llu bytes: %s", static_cast<unsigned long long>(bytes_written), esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            firmware.close();
            return FirmwareUpdateResult::OtaWriteFailed;
        }
        #else
        const int      read_count = to_read;
        delay(kMockChunkDelayMs);
        #endif

        bytes_written += static_cast<uint64_t>(read_count);
        bytes_since_progress_check += static_cast<uint32_t>(read_count);
        feed_watchdog();

        if (progressCallback && bytes_since_progress_check >= kProgressCheckBytes)
        {
            bytes_since_progress_check = 0;
            const uint32_t now = millis();
            if (now - last_progress_ms >= kProgressIntervalMs)
            {
                progressCallback(bytes_written, bytes_total);
                last_progress_ms = now;
                last_reported_bytes = bytes_written;
                feed_watchdog();
            }
        }
    }

    if (progressCallback && last_reported_bytes != bytes_written)
    {
        progressCallback(bytes_written, bytes_total);
    }

    feed_watchdog();
    #ifndef TEST_MOCK_FW_UPDATE
    err = esp_ota_end(ota_handle);
    firmware.close();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        return FirmwareUpdateResult::OtaEndFailed;
    }

    feed_watchdog();
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return FirmwareUpdateResult::SetBootPartitionFailed;
    }
    #endif

    ESP_LOGI(TAG, "firmware update staged: %llu bytes written", static_cast<unsigned long long>(bytes_written));
    return FirmwareUpdateResult::Ok;
}

} // namespace MicroSdCard
