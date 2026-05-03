#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

#include "AudioFifo.h"

namespace AudioManager
{

enum class Hardware
{
    Core2Internal,
    WM8960,
};

enum class I2SMode
{
    Stopped,
    Speaker,
    Mic,
};

static constexpr uint32_t kSampleRateHz = 16000;
static constexpr uint8_t  kMinVolume    = 0;
static constexpr uint8_t  kMaxVolume    = 10;

bool init(Hardware hardware = Hardware::Core2Internal);
void stop();

bool    enableSpeakerMode();
bool    enableMicMode();
I2SMode mode();

void pump_bt2s();
void pump_mic2bt();
bool startPumpTask(BaseType_t coreId = 1, UBaseType_t priority = 3);

void     hfp_incoming_audio(const uint8_t* buf, uint32_t len);
uint32_t hfp_outgoing_audio(uint8_t* buf, uint32_t len);

AudioFifo& bluetoothToSpeakerFifo();
AudioFifo& bluetoothToFileFifo();
AudioFifo& micToBluetoothFifo();
AudioFifo& micToFileFifo();

void    setVolume(uint8_t volume);
void    volumeUp();
void    volumeDown();
uint8_t volume();

void setMicMuted(bool muted);
void muteMic();
void unmuteMic();
bool micMuted();

} // namespace AudioManager
