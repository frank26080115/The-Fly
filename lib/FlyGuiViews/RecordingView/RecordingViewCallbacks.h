#pragma once

#include <stdint.h>

namespace AudioFileRecorder
{
enum class RecordingType : char;
}

namespace RecordingViewCallbacks
{

bool beginBluetoothRecording(AudioFileRecorder::RecordingType type);
bool beginMemoRecording(char typeCode);
bool promoteMemoRecordingToBluetooth();
bool stopRecording(bool disconnectBluetooth = false);
bool restoreBluetoothConnectable();

bool enableMicMode();
bool enableSpeakerMode();

bool setSpeakerMuted(bool muted);
bool toggleSpeakerMuted();
bool speakerMuted();

bool answerCall();

} // namespace RecordingViewCallbacks
