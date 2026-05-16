#pragma once

#include <Arduino.h>
#include "thefly_common.h"

#include <cstddef>
#include <cstdint>

#include "AudioFifo.h"

namespace AudioManager
{

enum class Hardware
{
    M5StackInternal,
    ExternalI2SCodec,
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

struct HfpAudioDiagnostics
{
    uint32_t incomingCallbacks          = 0;
    uint32_t incomingNullOrEmpty        = 0;
    uint32_t incomingNoDecoder          = 0;
    uint32_t incomingDecodeFrames       = 0;
    uint32_t incomingDecodeFailures     = 0;
    uint64_t incomingBytes              = 0;
    uint64_t incomingConsumedBytes      = 0;
    uint64_t incomingPcmSamples         = 0;
    uint64_t incomingQueuedSpkSamples   = 0;
    uint64_t incomingQueuedFileSamples  = 0;
    uint32_t outgoingCallbacks          = 0;
    uint32_t outgoingNullOrSmall        = 0;
    uint32_t outgoingNoEncoder          = 0;
    uint32_t outgoingUnderflows         = 0;
    uint32_t outgoingEncodeFrames       = 0;
    uint32_t outgoingEncodeFailures     = 0;
    uint64_t outgoingRequestedBytes     = 0;
    uint64_t outgoingReturnedBytes      = 0;
    uint64_t outgoingPcmSamplesRead     = 0;
    uint64_t speakerPumpCalls           = 0;
    uint64_t speakerI2sWriteBytes       = 0;
    uint64_t speakerI2sWriteFrames      = 0;
    uint32_t speakerI2sShortWrites      = 0;
    uint32_t speakerI2sWriteErrors      = 0;
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
HfpAudioDiagnostics hfpAudioDiagnostics();
void resetHfpAudioDiagnostics();

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
uint8_t micPeakLevel();
uint8_t micScaledPeakLevel();

} // namespace AudioManager
