#pragma once

#include <stdint.h>

namespace RecordingViewCallbacks
{

bool beginBluetoothRecording(char typeCode);
bool beginMemoRecording(char typeCode);
bool stopRecording(bool disconnectBluetooth = false);

bool enableMicMode();
bool enableSpeakerMode();

bool setSpeakerMuted(bool muted);
bool toggleSpeakerMuted();
bool speakerMuted();

bool answerCall();

} // namespace RecordingViewCallbacks
