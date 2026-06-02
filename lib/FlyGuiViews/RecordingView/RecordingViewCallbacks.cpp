#include "RecordingViewCallbacks.h"

#include "AudioFileRecorder.h"
#include "AudioManager.h"
#include "BluetoothManager.h"

namespace RecordingViewCallbacks
{
namespace
{

bool g_memo_bt_fifo_choked = false;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void restore_memo_bt_fifo();

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

bool beginBluetoothRecording(AudioFileRecorder::RecordingType type)
{
    restore_memo_bt_fifo();
    setSpeakerMuted(false);
    if (!AudioFileRecorder::startRecording(type))
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

bool promoteMemoRecordingToBluetooth()
{
    restore_memo_bt_fifo();
    setSpeakerMuted(false);
    const bool promoted = AudioManager::mode() == AudioManager::P2TMode::Mic || AudioManager::enableMicMode();
    if (promoted)
    {
        AudioFileRecorder::setRecordingType(AudioFileRecorder::RecordingType::Meeting);
    }
    return promoted;
}

bool stopRecording(bool disconnectBluetooth)
{
    setSpeakerMuted(false);
    restore_memo_bt_fifo();
    AudioManager::stop();
    const bool stopped = AudioFileRecorder::stopRecording();
    if (disconnectBluetooth)
    {
        BtManager::disconnect();
    }
    return stopped;
}

bool enableMicMode()
{
    return AudioManager::enableMicMode();
}

bool enableSpeakerMode()
{
    AudioManager::bluetoothToSpeakerFifo().clear();
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

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void restore_memo_bt_fifo()
{
    if (g_memo_bt_fifo_choked)
    {
        AudioManager::micToBluetoothFifo().setChoked(false);
        g_memo_bt_fifo_choked = false;
    }
}

} // namespace

} // namespace RecordingViewCallbacks
