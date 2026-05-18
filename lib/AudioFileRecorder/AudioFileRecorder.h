#pragma once

#include "thefly_common.h"

#include <Arduino.h>
#include <SdFat.h>

#include <cstddef>
#include <cstdint>

#include "AudioFifo.h"
#include "FileHistory.h"

namespace AudioFileRecorder
{

enum class RecordingType : char
{
    Meeting = 'C',
    Memo    = 'M',
    Todo    = 'T',
    Journal = 'J',
    Unknown = 'U',
};

enum PacketFlags : uint8_t
{
    kPacketFlagFifoOverflow  = 1 << 0,
    kPacketFlagFifoUnderflow = 1 << 1,
};

bool init(AudioFifo& hostFifo, AudioFifo& micFifo);
bool startRecording(RecordingType type);
bool startRecording(char typeCode);
void pump();
bool stopRecording(bool estop = false);
bool isRecording();
bool purePcmMode();
void setPurePcmMode(bool enabled);
bool queueMetaText(const char* text);

float writeDurationAverageMs();
float writeDurationMaxMs();
void  resetWriteDurationStats();
bool  longWrite();
bool  longWriteSinceReset();
void  resetLongWrite();
void  resetLongWriteSinceReset();
uint32_t lastLongWriteTimestampMs();

uint64_t    bytesWritten();
const char* currentSdPath();

bool grow_file(FsFile& file, uint64_t size);

} // namespace AudioFileRecorder
