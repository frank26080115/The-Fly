#include "RecordingViewCallbacks.h"

#include "AudioFileRecorder.h"
#include "AudioManager.h"
#include "BluetoothManager.h"

namespace RecordingViewCallbacks
{
namespace
{

bool    g_memo_bt_fifo_choked  = false;

void restore_memo_bt_fifo()
{
    if (g_memo_bt_fifo_choked)
    {
        AudioManager::micToBluetoothFifo().setChoked(false);
        g_memo_bt_fifo_choked = false;
    }
}

} // namespace

bool beginBluetoothRecording(char typeCode)
{
    restore_memo_bt_fifo();
    setSpeakerMuted(false);
    if (!AudioFileRecorder::startRecording(typeCode))
    {
        return false;
    }

    if (!AudioManager::enableSpeakerMode())
    {
        AudioFileRecorder::stopRecording(true);
        return false;
    }

    return true;
}

bool beginMemoRecording(char typeCode)
{
    setSpeakerMuted(false);
    AudioManager::micToBluetoothFifo().setChoked(true);
    g_memo_bt_fifo_choked = true;

    if (!AudioFileRecorder::startRecording(typeCode))
    {
        restore_memo_bt_fifo();
        return false;
    }

    if (!AudioManager::enableMicMode())
    {
        AudioFileRecorder::stopRecording(true);
        restore_memo_bt_fifo();
        return false;
    }

    return true;
}

bool stopRecording()
{
    setSpeakerMuted(false);
    restore_memo_bt_fifo();
    AudioManager::stop();
    return AudioFileRecorder::stopRecording();
}

bool enableMicMode()
{
    return AudioManager::enableMicMode();
}

bool enableSpeakerMode()
{
    return AudioManager::enableSpeakerMode();
}

bool setSpeakerMuted(bool muted)
{
    return AudioManager::setSpeakerMuted(muted);
}

bool toggleSpeakerMuted()
{
    return AudioManager::toggleSpeakerMuted();
}

bool speakerMuted()
{
    return AudioManager::speakerMuted();
}

bool answerCall()
{
    return BtManager::pickupPhone() == BtManager::Result::Ok;
}

} // namespace RecordingViewCallbacks
