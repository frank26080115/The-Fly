#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

#include "AudioFifo.h"

namespace AudioManager
{

enum class Hardware
{
    M5StackInternal,
    WM8960,
};

enum class P2TMode
{
    Stopped,
    Speaker,
    Mic,
};

enum class HfpCodec
{
    Cvsd,
    Msbc,
};

static constexpr uint32_t kSampleRateHz = 16000;
// we normalize all audio to 16KHz when it gets put into the FIFO (and recorded file)
// the FIFO will upsample or downsample as needed

static constexpr uint8_t kMinVolume = 0;
static constexpr uint8_t kMaxVolume = 30;
// 30 is chosen because Bluetooth sets volume 0-15, but I want the user interface to operate on 10% steps.
// 30 allows the Bluetooth host to set the volume in steps of 3, while the user interface can set the volume in steps of 2

bool init(Hardware hardware = Hardware::M5StackInternal);
void stop();

bool    enableSpeakerMode();
bool    enableMicMode();
P2TMode mode();

void pump_bt2spk();
void pump_mic2bt();
void pump_task();

void     hfp_incoming_audio(const uint8_t* buf, uint32_t len);
uint32_t hfp_outgoing_audio(uint8_t* buf, uint32_t len);
bool     setHfpAudioFormat(HfpCodec codec, uint32_t sampleRateHz);
HfpCodec hfpAudioCodec();
uint32_t hfpAudioSampleRateHz();

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
