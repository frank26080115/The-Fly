// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "NotchFilter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr float kPi = 3.14159265358979323846f;

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

inline int16_t saturate_int16(float sample)
{
    if (sample > static_cast<float>(std::numeric_limits<int16_t>::max()))
    {
        return std::numeric_limits<int16_t>::max();
    }
    if (sample < static_cast<float>(std::numeric_limits<int16_t>::min()))
    {
        return std::numeric_limits<int16_t>::min();
    }

    const float rounded = sample >= 0.0f ? sample + 0.5f : sample - 0.5f;
    return static_cast<int16_t>(rounded);
}

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

NotchFilter::NotchFilter(float frequencyHz, float quality, uint32_t sampleRateHz)
{
    configure(frequencyHz, quality, sampleRateHz);
}

void NotchFilter::process(int16_t* samples, size_t sampleCount)
{
    if (!samples || sampleCount == 0)
    {
        return;
    }

    for (size_t i = 0; i < sampleCount; ++i)
    {
        const float input  = static_cast<float>(samples[i]);
        const float output = (b0_ * input) + z1_;

        z1_ = (b1_ * input) - (a1_ * output) + z2_;
        z2_ = (b2_ * input) - (a2_ * output);

        samples[i] = saturate_int16(output);
    }
}

void NotchFilter::reset()
{
    z1_ = 0.0f;
    z2_ = 0.0f;
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void NotchFilter::configure(float frequencyHz, float quality, uint32_t sampleRateHz)
{
    // second-order IIR biquad notch filter

    const float sample_rate_hz = static_cast<float>(sampleRateHz == 0 ? 16000U : sampleRateHz);
    const float nyquist_hz     = sample_rate_hz * 0.5f;
    const float frequency_hz   = std::min(std::max(frequencyHz, 1.0f), nyquist_hz - 1.0f);
    const float q              = std::max(quality, 0.001f);
    const float omega          = (2.0f * kPi * frequency_hz) / sample_rate_hz;
    const float cos_omega      = cosf(omega);
    const float alpha          = sinf(omega) / (2.0f * q);
    const float a0             = 1.0f + alpha;

    b0_ = 1.0f / a0;
    b1_ = (-2.0f * cos_omega) / a0;
    b2_ = 1.0f / a0;
    a1_ = (-2.0f * cos_omega) / a0;
    a2_ = (1.0f - alpha) / a0;
    reset();
}
