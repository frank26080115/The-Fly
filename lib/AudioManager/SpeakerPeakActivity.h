#pragma once

#include "thefly_common.h"

#include <cstddef>
#include <cstdint>

namespace SpeakerPeakActivity
{

void init();

// Tracks mono signed 16-bit PCM peak activity. Passing nullptr with sampleCount 0
// advances decay state without touching audio.
void process(const int16_t* samples, size_t sampleCount);

uint16_t rawPeak();
uint8_t  rawPeakLevel();

} // namespace SpeakerPeakActivity
