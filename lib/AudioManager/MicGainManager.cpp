#include "MicGainManager.h"

#include <Arduino.h>
#include <algorithm>
#include <limits>
#include <mutex>

/*
This module does efficient processing of microphone data

The internal SPM1423 mic, it works, it's not very good. It exhibits heavy DC bias offsets that drift a lot, and the
volume varies a lot between different talking scenarios.

MicGainManager is both a high pass filter and a automatic gain control.

The AGC uses the detected mic peak level to determine a target gain. It also slews the current gain towards the target
gain slowly if going up, faster if going down (to respond to clipping better).

The gain changes only apply during zero crossings to avoid artifacting.

The high pass filter is there so that the gain is applied symmetrically.

The mathematics uses integer math for speed. The memory is modified in-place. Hence this module is fast and efficient, a
test showed that the performance hit of using this module is almost negligible. It is faster than what it needs to be by
about 114x.
*/

namespace MicGainManager
{
namespace
{

constexpr uint32_t kNominalSampleRateHz = 16000;
constexpr uint16_t kMaxSampleMagnitude  = 32767;
constexpr uint16_t kTargetPeak          = (kMaxSampleMagnitude * 3U) / 4U;
constexpr uint16_t kMinGainUnits        = kGainDivisor;
constexpr uint16_t kMaxGainUnits        = kGainDivisor *
                                            #ifndef TEST_NO_MIC_AGC
                                            5U;
                                            #else
                                            1U;
                                            #endif
constexpr uint16_t kInitialGainUnits    = (kGainDivisor * 3U) / 2U;
constexpr uint16_t kGainStepScale       = kGainDivisor / 128U;
constexpr uint16_t kGainStepUpUnits     = kGainStepScale * 3U;
constexpr uint16_t kGainStepDownUnits   = kGainStepScale * 8U;
constexpr uint16_t kHighPassFilterR     = 8151;
constexpr uint8_t  kSilenceGateThresholdPercentX10 = 
                                            #ifndef TEST_NO_MIC_AGC
                                            2; // 0.2%
                                            // note: use 10% if using 63 dB mic gain
                                            #else
                                            0;
                                            #endif
constexpr uint16_t kSilenceGateThreshold =
    (kMaxSampleMagnitude * static_cast<uint16_t>(kSilenceGateThresholdPercentX10)) / 1000U;
constexpr uint32_t kSilenceGateDurationMs    = 500;
constexpr uint32_t kSilenceGateSampleLimit   = (kNominalSampleRateHz * kSilenceGateDurationMs) / 1000U;

std::mutex g_mutex;

bool     g_bypass          = false;
bool     g_high_pass_filter_enabled = true;
bool     g_fixed_gain_mode = false;
uint16_t g_fixed_gain      = kInitialGainUnits;
uint16_t g_target_gain     = kInitialGainUnits;
uint16_t g_current_gain    = kInitialGainUnits;
uint16_t g_previous_gain   = kInitialGainUnits;
uint16_t g_raw_peak        = 0;
uint16_t g_scaled_peak     = 0;
int32_t  g_previous_hpf_x  = 0;
int32_t  g_previous_hpf_y  = 0;
int16_t  g_last_hpf_sample = 0;
bool     g_have_hpf_state  = false;
bool     g_have_last_hpf   = false;
uint32_t g_ignore_until_ms = 0;
uint32_t g_silence_gate_sample_count = 0;
bool     g_silence_gate_active       = false;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void     init_unlocked();
static bool     ignoring_samples(uint32_t nowMs);
static void     decay_peak(uint16_t& peak, size_t frames);
static int16_t  high_pass_filter(int16_t sample);
static void     reset_high_pass_filter_state();
static void     reset_silence_gate_state();
static void     update_silence_gate(int16_t sample);
static bool     zero_crossed(int16_t sample);
static uint16_t target_peak_for_state();
static uint16_t minimum_gain_for_state();
static void     update_target_gain();
static void     update_current_gain();
static uint16_t gain_to_units(float gain);
static float    units_to_gain(uint16_t gain);
static uint16_t scaled_abs_peak(int16_t sample, uint16_t gain);
static uint8_t  peak_to_level(uint16_t peak);
static uint16_t clamp_gain_units(uint16_t gain);
static uint16_t sample_abs_peak(int16_t sample);
static int16_t  saturate_int16(int32_t sample);
static int32_t  saturate_int32(int64_t sample);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void init()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    init_unlocked();
}

void process(int16_t* samples, size_t sampleCount)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_bypass)
    {
        return;
    }

    decay_peak(g_raw_peak, sampleCount);
    decay_peak(g_scaled_peak, sampleCount);
    if (g_silence_gate_active)
    {
        g_raw_peak    = 0;
        g_scaled_peak = 0;
    }

    if (!samples || sampleCount == 0)
    {
        return;
    }

    const bool     ignoring          = ignoring_samples(millis());
    uint16_t       raw_chunk_peak    = 0;
    uint16_t       scaled_chunk_peak = 0;
    uint16_t       active_gain       = g_previous_gain;
    const uint16_t next_gain         = g_current_gain;
    bool           gain_switched     = active_gain == next_gain;

    for (size_t i = 0; i < sampleCount; ++i)
    {
        const int16_t raw_sample = samples[i];
        const int16_t hpf_sample = high_pass_filter(raw_sample);
        update_silence_gate(hpf_sample);
        if (ignoring)
        {
            samples[i] = 0;
            continue;
        }

        raw_chunk_peak = std::max<uint16_t>(raw_chunk_peak, sample_abs_peak(hpf_sample));

        if (active_gain != next_gain && zero_crossed(hpf_sample))
        {
            active_gain     = next_gain;
            g_previous_gain = next_gain;
            gain_switched   = true;
        }

        const int32_t scaled =
            (static_cast<int32_t>(hpf_sample) * static_cast<int32_t>(active_gain)) / static_cast<int32_t>(kGainDivisor);
        const int16_t scaled_sample = saturate_int16(scaled);
        samples[i]                  = scaled_sample;

        scaled_chunk_peak = std::max<uint16_t>(scaled_chunk_peak, sample_abs_peak(scaled_sample));
        g_last_hpf_sample = hpf_sample;
        g_have_last_hpf   = true;
    }

    if (ignoring)
    {
        g_have_last_hpf = false;
        return;
    }

    if (!gain_switched)
    {
        g_previous_gain = next_gain;
    }

    g_raw_peak    = std::max<uint16_t>(g_raw_peak, raw_chunk_peak);
    g_scaled_peak = std::max<uint16_t>(g_scaled_peak, scaled_chunk_peak);
    update_target_gain();
    update_current_gain();

    if (g_silence_gate_active)
    {
        g_raw_peak    = 0;
        g_scaled_peak = 0;
    }
}

void ignoreSamplesFor(uint32_t durationMs)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ignore_until_ms = millis() + durationMs;
    reset_high_pass_filter_state();
    reset_silence_gate_state();
}

void setBypass(bool enabled)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_bypass != enabled)
    {
        reset_high_pass_filter_state();
        reset_silence_gate_state();
    }
    g_bypass = enabled;
}

bool bypass()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_bypass;
}

void setHighPassFilterEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_high_pass_filter_enabled != enabled)
    {
        reset_high_pass_filter_state();
        reset_silence_gate_state();
    }
    g_high_pass_filter_enabled = enabled;
}

bool highPassFilterEnabled()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_high_pass_filter_enabled;
}

void setFixedGainMode(bool enabled)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_fixed_gain_mode = enabled;
    if (enabled)
    {
        g_target_gain = g_fixed_gain;
    }
}

bool fixedGainMode()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_fixed_gain_mode;
}

void setFixedGain(float gain)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_fixed_gain = gain_to_units(gain);
    if (g_fixed_gain_mode)
    {
        g_target_gain = g_fixed_gain;
    }
}

float fixedGain()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return units_to_gain(g_fixed_gain);
}

void setTargetGain(float gain)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_target_gain = gain_to_units(gain);
}

float targetGain()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return units_to_gain(g_target_gain);
}

float currentGain()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return units_to_gain(g_current_gain);
}

float previousGain()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return units_to_gain(g_previous_gain);
}

uint16_t currentGainUnits()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_current_gain;
}

uint16_t previousGainUnits()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_previous_gain;
}

uint16_t targetGainUnits()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_target_gain;
}

uint16_t fixedGainUnits()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_fixed_gain;
}

uint16_t rawPeak()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_silence_gate_active)
    {
        return 0;
    }
    return g_raw_peak;
}

uint16_t scaledPeak()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_silence_gate_active)
    {
        return 0;
    }
    return g_bypass ? g_raw_peak
                    : std::max<uint16_t>(
                          g_scaled_peak,
                          scaled_abs_peak(static_cast<int16_t>(std::min<uint16_t>(g_raw_peak, kMaxSampleMagnitude)),
                                          g_current_gain));
}

uint8_t rawPeakLevel()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_silence_gate_active)
    {
        return 0;
    }
    return peak_to_level(g_raw_peak);
}

uint8_t scaledPeakLevel()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_silence_gate_active)
    {
        return 0;
    }
    const uint16_t              peak =
        g_bypass ? g_raw_peak
                 : std::max<uint16_t>(
                       g_scaled_peak,
                       scaled_abs_peak(static_cast<int16_t>(std::min<uint16_t>(g_raw_peak, kMaxSampleMagnitude)),
                                       g_current_gain));
    return peak_to_level(peak);
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void init_unlocked()
{
    g_bypass          = false;
    g_high_pass_filter_enabled = true;
    g_fixed_gain_mode = false;
    g_fixed_gain      = kInitialGainUnits;
    g_target_gain     = kInitialGainUnits;
    g_current_gain    = kInitialGainUnits;
    g_previous_gain   = kInitialGainUnits;
    g_raw_peak        = 0;
    g_scaled_peak     = 0;
    g_previous_hpf_x  = 0;
    g_previous_hpf_y  = 0;
    g_last_hpf_sample = 0;
    g_have_hpf_state  = false;
    g_have_last_hpf   = false;
    g_ignore_until_ms = 0;
    reset_silence_gate_state();
}

bool ignoring_samples(uint32_t nowMs)
{
    return static_cast<int32_t>(nowMs - g_ignore_until_ms) < 0;
}

void decay_peak(uint16_t& peak, size_t frames)
{
    uint32_t decay = 1;
    if (frames > 0)
    {
        decay = std::max<uint32_t>(decay,
                                   (static_cast<uint32_t>(kMaxSampleMagnitude) * static_cast<uint32_t>(frames) * 2U) /
                                       (kNominalSampleRateHz * 3U));
    }

    peak = decay >= peak ? 0 : static_cast<uint16_t>(peak - decay);
}

int16_t high_pass_filter(int16_t sample)
{
    if (!g_high_pass_filter_enabled)
    {
        return sample;
    }

    const int32_t x = sample;
    if (!g_have_hpf_state)
    {
        g_previous_hpf_x = x;
        g_previous_hpf_y = 0;
        g_have_hpf_state = true;
        return 0;
    }

    const int64_t filtered = static_cast<int64_t>(x) - g_previous_hpf_x +
                             (static_cast<int64_t>(kHighPassFilterR) * g_previous_hpf_y) / kGainDivisor;
    g_previous_hpf_x       = x;
    g_previous_hpf_y       = saturate_int32(filtered);
    return saturate_int16(g_previous_hpf_y);
}

void reset_high_pass_filter_state()
{
    g_previous_hpf_x = 0;
    g_previous_hpf_y = 0;
    g_have_hpf_state = false;
    g_have_last_hpf  = false;
}

void reset_silence_gate_state()
{
    g_silence_gate_sample_count = 0;
    g_silence_gate_active       = false;
}

void update_silence_gate(int16_t sample)
{
    if (sample_abs_peak(sample) > kSilenceGateThreshold)
    {
        reset_silence_gate_state();
        return;
    }

    if (g_silence_gate_sample_count <= kSilenceGateSampleLimit)
    {
        ++g_silence_gate_sample_count;
    }
    g_silence_gate_active = g_silence_gate_sample_count > kSilenceGateSampleLimit;
}

bool zero_crossed(int16_t sample)
{
    if (sample == 0)
    {
        return true;
    }
    if (!g_have_last_hpf)
    {
        return false;
    }

    return (g_last_hpf_sample < 0 && sample > 0) || (g_last_hpf_sample > 0 && sample < 0);
}

void update_target_gain()
{
    const uint16_t target_peak = target_peak_for_state();
    if (target_peak == 0)
    {
        g_target_gain = 0;
        return;
    }

    if (g_fixed_gain_mode)
    {
        g_target_gain = g_fixed_gain;
        return;
    }

    if (g_raw_peak == 0)
    {
        g_target_gain = kMaxGainUnits;
        return;
    }

    const uint32_t gain =
        (static_cast<uint32_t>(target_peak) * static_cast<uint32_t>(kGainDivisor) + (g_raw_peak / 2U)) / g_raw_peak;
    g_target_gain = clamp_gain_units(static_cast<uint16_t>(std::min<uint32_t>(gain, kMaxGainUnits)));
}

void update_current_gain()
{
    if (g_current_gain < g_target_gain)
    {
        g_current_gain = static_cast<uint16_t>(std::min<uint16_t>(g_target_gain, g_current_gain + kGainStepUpUnits));
        return;
    }

    if (g_current_gain > g_target_gain)
    {
        const uint16_t min_gain = minimum_gain_for_state();
        const uint16_t next_gain = g_current_gain > kGainStepDownUnits
                                       ? static_cast<uint16_t>(g_current_gain - kGainStepDownUnits)
                                       : min_gain;
        g_current_gain           = std::max<uint16_t>(g_target_gain, next_gain);
    }
}

uint16_t target_peak_for_state()
{
    return g_silence_gate_active ? 0 : kTargetPeak;
}

uint16_t minimum_gain_for_state()
{
    return g_silence_gate_active ? 0 : kMinGainUnits;
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

uint16_t gain_to_units(float gain)
{
    if (gain <= kMinGain)
    {
        return kMinGainUnits;
    }
    if (gain >= kMaxGain)
    {
        return kMaxGainUnits;
    }

    return clamp_gain_units(static_cast<uint16_t>((gain * static_cast<float>(kGainDivisor)) + 0.5f));
}

float units_to_gain(uint16_t gain)
{
    return static_cast<float>(gain) / static_cast<float>(kGainDivisor);
}

uint16_t scaled_abs_peak(int16_t sample, uint16_t gain)
{
    const int32_t scaled =
        (static_cast<int32_t>(sample) * static_cast<int32_t>(gain)) / static_cast<int32_t>(kGainDivisor);
    return sample_abs_peak(saturate_int16(scaled));
}

uint8_t peak_to_level(uint16_t peak)
{
    return static_cast<uint8_t>(std::min<uint32_t>(100U, (static_cast<uint32_t>(peak) * 100U) / kMaxSampleMagnitude));
}

uint16_t clamp_gain_units(uint16_t gain)
{
    return std::min<uint16_t>(kMaxGainUnits, std::max<uint16_t>(kMinGainUnits, gain));
}

uint16_t sample_abs_peak(int16_t sample)
{
    if (sample == std::numeric_limits<int16_t>::min())
    {
        return 32768U;
    }

    const int16_t magnitude = sample < 0 ? -sample : sample;
    return static_cast<uint16_t>(magnitude);
}

int16_t saturate_int16(int32_t sample)
{
    if (sample > std::numeric_limits<int16_t>::max())
    {
        return std::numeric_limits<int16_t>::max();
    }
    if (sample < std::numeric_limits<int16_t>::min())
    {
        return std::numeric_limits<int16_t>::min();
    }

    return static_cast<int16_t>(sample);
}

int32_t saturate_int32(int64_t sample)
{
    if (sample > std::numeric_limits<int32_t>::max())
    {
        return std::numeric_limits<int32_t>::max();
    }
    if (sample < std::numeric_limits<int32_t>::min())
    {
        return std::numeric_limits<int32_t>::min();
    }

    return static_cast<int32_t>(sample);
}

} // namespace

} // namespace MicGainManager
