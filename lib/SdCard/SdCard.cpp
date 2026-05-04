#include "SdCard.h"

#include <M5Unified.h>
#include <SPI.h>

#include "ClockAgent.h"
#include "esp_log.h"

namespace SdCard
{
namespace
{

constexpr const char* TAG = "SdCard";

constexpr int kCore2SdSclk = 18;
constexpr int kCore2SdMiso = 38;
constexpr int kCore2SdMosi = 23;
constexpr int kCore2SdCs   = 4;

constexpr uint32_t kSdFrequencyMHz = 40;

SdFs g_sd;
bool g_ready = false;

void sdFatDateTime(uint16_t* date, uint16_t* time, uint8_t* ms10)
{
    m5::rtc_datetime_t now;
    if (!Clock.getDateTime(&now))
    {
        now.date.year   = 2026;
        now.date.month  = 1;
        now.date.date   = 1;
        now.time.hours  = 0;
        now.time.minutes = 0;
        now.time.seconds = 0;
    }

    *date = FS_DATE(now.date.year, now.date.month, now.date.date);
    *time = FS_TIME(now.time.hours, now.time.minutes, now.time.seconds);
    *ms10 = 0;
}

} // namespace

bool begin()
{
    if (g_ready)
    {
        return true;
    }

    SPI.begin(kCore2SdSclk, kCore2SdMiso, kCore2SdMosi, kCore2SdCs);
    FsDateTime::setCallback(sdFatDateTime);

    if (!g_sd.begin(SdSpiConfig(kCore2SdCs, SHARED_SPI, SD_SCK_MHZ(kSdFrequencyMHz), &SPI)))
    {
        ESP_LOGE(TAG, "microSD init failed");
        return false;
    }

    ESP_LOGI(TAG, "microSD space: total=%llu used=%llu free=%llu", static_cast<unsigned long long>(totalBytes()), static_cast<unsigned long long>(usedBytes()), static_cast<unsigned long long>(freeBytes()));
    g_ready = true;
    return true;
}

bool isReady()
{
    return g_ready;
}

SdFs& fs()
{
    return g_sd;
}

uint64_t totalBytes()
{
    return static_cast<uint64_t>(g_sd.clusterCount()) * static_cast<uint64_t>(g_sd.bytesPerCluster());
}

uint64_t usedBytes()
{
    const uint64_t total = totalBytes();
    const uint64_t free  = freeBytes();
    return total > free ? total - free : 0;
}

uint64_t freeBytes()
{
    const int64_t freeClusters = g_sd.freeClusterCount();
    if (freeClusters <= 0)
    {
        return 0;
    }
    return static_cast<uint64_t>(freeClusters) * static_cast<uint64_t>(g_sd.bytesPerCluster());
}

} // namespace SdCard
