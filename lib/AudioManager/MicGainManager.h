#pragma once

#include <cstddef>
#include <cstdint>

namespace MicGainManager
{

static constexpr uint16_t kGainDivisor = 8192;
static constexpr float    kMinGain     = 1.0f;
static constexpr float    kMaxGain     = 3.0f;
static constexpr float    kInitialGain = 1.5f;
static constexpr uint32_t kStartupTransientIgnoreMs = 200;

void init();

// Processes mono signed 16-bit PCM in place. Passing nullptr with sampleCount 0
// advances decay/gain state without touching audio.
void process(int16_t* samples, size_t sampleCount);
void ignoreSamplesFor(uint32_t durationMs = kStartupTransientIgnoreMs);

void setBypass(bool enabled);
bool bypass();

void setFixedGainMode(bool enabled);
bool fixedGainMode();

void  setFixedGain(float gain);
float fixedGain();

void  setTargetGain(float gain);
float targetGain();

float currentGain();
float previousGain();

uint16_t currentGainUnits();
uint16_t previousGainUnits();
uint16_t targetGainUnits();
uint16_t fixedGainUnits();

uint16_t rawPeak();
uint16_t scaledPeak();
uint8_t  rawPeakLevel();
uint8_t  scaledPeakLevel();

} // namespace MicGainManager
