#include "RecordingViewCallbacks.h"

#include "AudioFileRecorder.h"
#include "AudioManager.h"
#include "BluetoothManager.h"
#include "HapticsWrapper.h"

namespace RecordingViewCallbacks
{
namespace
{

bool g_memo_bt_fifo_choked = false;
bool g_have_speaker_mute_before_mic = false;
bool g_speaker_muted_before_mic     = false;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void restore_memo_bt_fifo();
static void remember_speaker_mute_before_mic();
static void restore_speaker_mute_after_mic();
static void clear_speaker_mute_before_mic();

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

bool beginBluetoothRecording(AudioFileRecorder::RecordingType type)
{
    clear_speaker_mute_before_mic();
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

    haptic_play_buzz();
    return true;
}

bool beginMemoRecording(char typeCode)
{
    clear_speaker_mute_before_mic();
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
    clear_speaker_mute_before_mic();
    setSpeakerMuted(false);
    const bool promoted = AudioManager::mode() == AudioManager::P2TMode::Mic || AudioManager::enableMicMode();
    if (promoted)
    {
        AudioFileRecorder::setRecordingType(AudioFileRecorder::RecordingType::Meeting);
        haptic_play_buzz();
    }
    return promoted;
}

bool stopRecording(bool disconnectBluetooth)
{
    if (disconnectBluetooth)
    {
        BtManager::disconnectNonConnectable();
    }
    clear_speaker_mute_before_mic();
    setSpeakerMuted(false);
    restore_memo_bt_fifo();
    AudioManager::stop();
    return AudioFileRecorder::stopRecording();
}

bool restoreBluetoothConnectable()
{
    return BtManager::setConnectableNonDiscoverable() == BtManager::Result::Ok;
}

bool enableMicMode()
{
    const bool enteringMic = AudioManager::mode() != AudioManager::P2TMode::Mic && !g_have_speaker_mute_before_mic;
    if (enteringMic)
    {
        remember_speaker_mute_before_mic();
    }

    const bool enabled = AudioManager::enableMicMode();
    if (!enabled && enteringMic)
    {
        clear_speaker_mute_before_mic();
    }
    return enabled;
}

bool enableSpeakerMode()
{
    const bool leavingMic = AudioManager::mode() == AudioManager::P2TMode::Mic;
    AudioManager::bluetoothToSpeakerFifo().clear();
    const bool enabled = AudioManager::enableSpeakerMode();
    if (enabled && leavingMic)
    {
        restore_speaker_mute_after_mic();
    }
    return enabled;
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

void remember_speaker_mute_before_mic()
{
    g_speaker_muted_before_mic     = AudioManager::speakerMuted();
    g_have_speaker_mute_before_mic = true;
}

void restore_speaker_mute_after_mic()
{
    if (!g_have_speaker_mute_before_mic)
    {
        return;
    }

    AudioManager::setSpeakerMuted(g_speaker_muted_before_mic);
    clear_speaker_mute_before_mic();
}

void clear_speaker_mute_before_mic()
{
    g_have_speaker_mute_before_mic = false;
    g_speaker_muted_before_mic     = false;
}

} // namespace

} // namespace RecordingViewCallbacks
