#include "MicroSdCard.h"

#include <M5Unified.h>
#include <SPI.h>

#include "ClockAgent.h"
#include "esp_log.h"

namespace MicroSdCard
{
namespace
{

constexpr const char* TAG = "MicroSdCard";

constexpr int kCore2SdSclk = 18;
constexpr int kCore2SdMiso = 38;
constexpr int kCore2SdMosi = 23;
constexpr int kCore2SdCs   = 4;

constexpr uint32_t kSdFrequenciesMHz[] = {
    //40,
    20, 10, 4};

SdFs g_sd;
bool g_ready = false;
uint32_t g_frequency_mhz = 0;

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

bool tryBeginAtFrequency(uint32_t frequencyMHz)
{
    g_sd.end();

    const SdSpiConfig config(kCore2SdCs, SHARED_SPI, SD_SCK_MHZ(frequencyMHz), &SPI);
    if (!g_sd.cardBegin(config))
    {
        ESP_LOGW(TAG, "microSD card init failed at %lu MHz: error=0x%02X data=0x%02X", static_cast<unsigned long>(frequencyMHz), g_sd.sdErrorCode(), g_sd.sdErrorData());
        return false;
    }

    if (!g_sd.volumeBegin())
    {
        ESP_LOGW(TAG, "microSD filesystem mount failed at %lu MHz: cardType=%u sectors=%lu error=0x%02X data=0x%02X", static_cast<unsigned long>(frequencyMHz), g_sd.card()->type(), static_cast<unsigned long>(g_sd.card()->sectorCount()), g_sd.sdErrorCode(), g_sd.sdErrorData());
        g_sd.end();
        return false;
    }

    ESP_LOGI(TAG, "microSD initialized at %lu MHz", static_cast<unsigned long>(frequencyMHz));
    g_frequency_mhz = frequencyMHz;
    return true;
}

void enableCore2SdPower()
{
    if (M5.Power.getType() == m5::Power_Class::pmic_axp192)
    {
        M5.Power.Axp192.setLDO2(3300);
        delay(50);
    }
}

} // namespace

bool begin()
{
    if (g_ready)
    {
        return true;
    }

    enableCore2SdPower();
    pinMode(kCore2SdCs, OUTPUT);
    digitalWrite(kCore2SdCs, HIGH);
    SPI.begin(kCore2SdSclk, kCore2SdMiso, kCore2SdMosi, kCore2SdCs);
    delay(10);
    FsDateTime::setCallback(sdFatDateTime);

    for (uint32_t frequencyMHz : kSdFrequenciesMHz)
    {
        if (tryBeginAtFrequency(frequencyMHz))
        {
            ESP_LOGI(TAG, "microSD space: total=%llu used=%llu free=%llu ; freq=%lu MHz", static_cast<unsigned long long>(totalBytes()), static_cast<unsigned long long>(usedBytes()), static_cast<unsigned long long>(freeBytes()), static_cast<unsigned long>(g_frequency_mhz));
            g_ready = true;
            return true;
        }
    }

    ESP_LOGE(TAG, "microSD init failed");
    return false;
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

} // namespace MicroSdCard
