#include "SpeakerPeakActivity.h"

#include <algorithm>
#include <limits>
#include <mutex>

namespace SpeakerPeakActivity
{
namespace
{

constexpr uint32_t kNominalSampleRateHz = 16000;
constexpr uint16_t kMaxSampleMagnitude  = 32767;

std::mutex g_mutex;
uint16_t   g_raw_peak = 0;

uint16_t sample_abs_peak(int16_t sample)
{
    if (sample == std::numeric_limits<int16_t>::min())
    {
        return 32768U;
    }

    const int16_t magnitude = sample < 0 ? -sample : sample;
    return static_cast<uint16_t>(magnitude);
}

void decay_peak(uint16_t& peak, size_t frames)
{
    uint32_t decay = 1;
    if (frames > 0)
    {
        decay = std::max<uint32_t>(decay, (static_cast<uint32_t>(kMaxSampleMagnitude) * static_cast<uint32_t>(frames) * 2U) / (kNominalSampleRateHz * 3U));
    }

    peak = decay >= peak ? 0 : static_cast<uint16_t>(peak - decay);
}

uint8_t peak_to_level(uint16_t peak)
{
    return static_cast<uint8_t>(std::min<uint32_t>(100U, (static_cast<uint32_t>(peak) * 100U) / kMaxSampleMagnitude));
}

} // namespace

void init()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_raw_peak = 0;
}

void process(const int16_t* samples, size_t sampleCount)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    decay_peak(g_raw_peak, sampleCount);

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

} // namespace SpeakerPeakActivity
