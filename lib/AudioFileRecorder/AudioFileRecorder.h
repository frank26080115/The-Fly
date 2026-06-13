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

// these are the first letter of the file name
enum class RecordingType : char
{
    Meeting  = 'C',
    Memo     = 'M',
    Todo     = 'T',
    Journal  = 'J',
    Idea     = 'I',
    Reminder = 'R',
    Unknown  = 'U',
};

bool init(AudioFifo& hostFifo, AudioFifo& micFifo);
bool startRecording(RecordingType type);
bool startRecording(char typeCode);
void setRecordingType(RecordingType type);
void setRecordingType(char typeCode);
void setMemoType(MemoType type);
bool needsPump();
void pump();
bool stopRecording(bool estop = false);
bool isRecording();
bool purePcmMode();                // debug use only
void setPurePcmMode(bool enabled); // debug use only

float    writeDurationAverageMs();
float    writeDurationMaxMs();
void     resetWriteDurationStats();
bool     longWrite();
bool     longWriteSinceReset();
void     resetLongWrite();
void     resetLongWriteSinceReset();
uint32_t lastLongWriteTimestampMs();

uint64_t    bytesWritten();
const char* currentSdPath();
uint32_t    fifoOverflowEvents();

bool grow_file(FsFile& file, uint64_t size); // unused, too slow

bool ensureAudioBuffers();
void releaseAudioBuffers();

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
// there are other pieces of code that borrow these shared buffers
uint8_t* wavPlaintextAudioBuffer();
uint8_t* wavEncryptedAudioBuffer();
size_t   wavPlaintextAudioBufferSize();
size_t   wavEncryptedAudioBufferSize();
#endif

} // namespace AudioFileRecorder
