#include <Arduino.h>

#include "AudioManager.h"
#include "MicGainManager.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG = "test_micfilterperformance";
constexpr size_t      kMonoBufferSamples = 240;
constexpr uint32_t    kTestDurationMs    = 10000;

uint32_t g_prng = 0x4D494346;

int16_t next_random_sample()
{
    g_prng = g_prng * 1664525UL + 1013904223UL;
    return static_cast<int16_t>(g_prng >> 16);
}

void fill_random(int16_t* buffer, size_t sampleCount)
{
    for (size_t i = 0; i < sampleCount; ++i)
    {
        buffer[i] = next_random_sample();
    }
}

} // namespace

void test_micfilterperformance()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.printf("%s: starting mic filter performance test\n", TAG);

    if (!AudioManager::init(AudioManager::Hardware::M5StackInternal))
    {
        Serial.printf("%s: AudioManager init failed\n", TAG);
        idle_forever();
    }

    int16_t buffer[kMonoBufferSamples];
    fill_random(buffer, kMonoBufferSamples);

    MicGainManager::setBypass(false);
    MicGainManager::init();

    uint64_t       process_count = 0;
    const uint32_t start_ms      = millis();
    while (static_cast<uint32_t>(millis() - start_ms) < kTestDurationMs)
    {
        MicGainManager::process(buffer, kMonoBufferSamples);
        ++process_count;
    }

    const uint32_t elapsed_ms = millis() - start_ms;
    const double   expected_process_count =
        (static_cast<double>(AudioManager::kSampleRateHz) * static_cast<double>(elapsed_ms)) /
        (static_cast<double>(kMonoBufferSamples) * 1000.0);
    const double process_per_second = (static_cast<double>(process_count) * 1000.0) / static_cast<double>(elapsed_ms);
    const double realtime_factor    = static_cast<double>(process_count) / expected_process_count;

    Serial.printf("%s: elapsed_ms=%lu buffer_samples=%u sample_rate=%lu\n",
                  TAG,
                  static_cast<unsigned long>(elapsed_ms),
                  static_cast<unsigned>(kMonoBufferSamples),
                  static_cast<unsigned long>(AudioManager::kSampleRateHz));
    Serial.printf("%s: process_count=%llu expected_for_realtime=%.3f\n",
                  TAG,
                  static_cast<unsigned long long>(process_count),
                  expected_process_count);
    Serial.printf("%s: process_per_second=%.3f realtime_factor=%.3f\n",
                  TAG,
                  process_per_second,
                  realtime_factor);

    idle_forever();
}
