#pragma once

#include "thefly_common.h"

#include <stdint.h>

namespace WavPlayback
{

static constexpr uint32_t kWavHeaderBytes = WAV_RIFF_HEADER_LENGTH;

bool start(const char* path);
void stop();
void pump();

bool active();
bool playing();
bool paused();
bool finished();

void setPlaying(bool playing);
void togglePlaying();
void setPositionMs(uint32_t positionMs);
void setVolume(uint8_t volume);

uint32_t durationMs();
uint32_t positionMs();
uint8_t  volume();
const char* path();
const char* lastError();

} // namespace WavPlayback
