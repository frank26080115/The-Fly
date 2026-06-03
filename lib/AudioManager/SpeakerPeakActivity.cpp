#include "SpeakerPeakActivity.h"

#include <Arduino.h>
#include <algorithm>
#include <limits>
#include <mutex>

namespace SpeakerPeakActivity
{
namespace
{

constexpr uint32_t kNominalSampleRateHz = 16000;
constexpr uint32_t kIdleDecayIntervalMs = 20;
constexpr uint16_t kMaxSampleMagnitude  = 32767;

std::mutex g_mutex;
uint16_t   g_raw_peak = 0;
uint32_t   g_last_decay_ms = 0;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static uint16_t sample_abs_peak(int16_t sample);
static void     decay_peak_internal(size_t frames);
static uint8_t  peak_to_level(uint16_t peak);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void init()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_raw_peak       = 0;
    g_last_decay_ms  = millis();
}

void process(const int16_t* samples, size_t sampleCount)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    decay_peak_internal(sampleCount);

    if (!samples || sampleCount == 0)
    {
        return;
    }

    uint16_t chunk_peak = 0;
    for (size_t i = 0; i < sampleCount; ++i)
    {
        chunk_peak = std::max<uint16_t>(chunk_peak, sample_abs_peak(samples[i]));
    }

    g_raw_peak = std::max<uint16_t>(g_raw_peak, chunk_peak);
}

void decay_peak()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    const uint32_t now        = millis();
    const uint32_t elapsed_ms = now - g_last_decay_ms;
    if (elapsed_ms < kIdleDecayIntervalMs)
    {
        return;
    }

    const size_t frames = static_cast<size_t>((static_cast<uint64_t>(elapsed_ms) * kNominalSampleRateHz) / 1000U);
    decay_peak_internal(frames);
}

// -----------------------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------------------

uint16_t rawPeak()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_raw_peak;
}

uint8_t rawPeakLevel()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return peak_to_level(g_raw_peak);
}

namespace
{

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

uint16_t sample_abs_peak(int16_t sample)
{
    if (sample == std::numeric_limits<int16_t>::min())
    {
        return 32768U;
    }

    const int16_t magnitude = sample < 0 ? -sample : sample;
    return static_cast<uint16_t>(magnitude);
}

void decay_peak_internal(size_t frames)
{
    uint32_t decay = 1;
    if (frames > 0)
    {
        decay = std::max<uint32_t>(decay,
                                   (static_cast<uint32_t>(kMaxSampleMagnitude) * static_cast<uint32_t>(frames) * 2U) /
                                       (kNominalSampleRateHz * 3U));
    }

    g_raw_peak      = decay >= g_raw_peak ? 0 : static_cast<uint16_t>(g_raw_peak - decay);
    g_last_decay_ms = millis();
}

uint8_t peak_to_level(uint16_t peak)
{
    return static_cast<uint8_t>(std::min<uint32_t>(100U, (static_cast<uint32_t>(peak) * 100U) / kMaxSampleMagnitude));
}

} // namespace

} // namespace SpeakerPeakActivity
