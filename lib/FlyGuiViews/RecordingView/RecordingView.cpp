#include "RecordingView.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "AudioFileRecorder.h"
#include "AudioManager.h"
#include "BluetoothManager.h"
#include "CallManager.h"
#include "ModalDialog.h"
#include "RecordingViewCallbacks.h"
#include "sprites.h"

extern ModalDialog* get_view_modal_dialog();

namespace
{
constexpr int16_t  kBorderX            = 0;
constexpr int16_t  kBorderY            = FlyGui::kTopBarHeight;
constexpr int16_t  kBorderSize         = 10;
constexpr int16_t  kButtonY            = 130;
constexpr int16_t  kMicButtonX         = 10;
constexpr int16_t  kMidButtonX         = 110;
constexpr int16_t  kExitButtonX        = 210;
constexpr int16_t  kSideButtonX        = 260;
constexpr int16_t  kBluetoothY         = FlyGui::kTopBarHeight + 10;
constexpr int16_t  kSideButtonY        = 70;
constexpr int16_t  kSideTouchSize      = 100;
constexpr int16_t  kTextX              = 16;
constexpr int16_t  kFileTextY          = 28;
constexpr int16_t  kElapsedTextY       = 62;
constexpr int16_t  kInfoTextY          = 96;
constexpr int16_t  kTextWidth          = 240;
constexpr int16_t  kTextHeight         = 28;
constexpr float    kFileTextSize       = 1.0f;
constexpr uint8_t  kFileTextFont       = 2;
constexpr float    kDurationTextSize   = 1.0f;
constexpr uint8_t  kDurationTextFont   = 4;
constexpr float    kCallerInfoTextSize = 1.0f;
constexpr uint8_t  kCallerInfoTextFont = 2;
constexpr uint32_t kCallerInfoCycleMs  = 3000;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static const char* path_basename(const char* path);

} // namespace

RecordingView* RecordingView::activeInstance_ = nullptr;

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

RecordingView::RecordingView()
    : FlyGuiView(FLYGUI_VIEW_RECORDING), micButton_(AudioDeviceButton::Device::Mic, kMicButtonX, kButtonY),
      speakerButton_(AudioDeviceButton::Device::Speaker, kMidButtonX, kButtonY),
      exitButton_(kExitButtonX, kButtonY, SPRITE_XCIRCLE_100_WIDTH, SPRITE_XCIRCLE_100_HEIGHT),
      bluetoothIcon_(kSideButtonX, kBluetoothY, SPRITE_BLUETOOTH_50_WIDTH, SPRITE_BLUETOOTH_50_HEIGHT),
      answerCallButton_(kSideButtonX, kSideButtonY), memoTypeButton_(kSideButtonX, kSideButtonY),
      fileNameText_(kTextX, kFileTextY, kTextWidth, kTextHeight, kFileTextSize, kFileTextFont, 64),
      durationText_(kTextX, kElapsedTextY, kTextWidth, kTextHeight, kDurationTextSize, kDurationTextFont),
      callerInfoText_(kTextX, kInfoTextY, kTextWidth, kTextHeight, kCallerInfoTextSize, kCallerInfoTextFont, 80)
{
    activeInstance_ = this;

    micButton_.setCallback(micThunk);
    addItem(micButton_);

    speakerButton_.setCallback(speakerThunk);
    addItem(speakerButton_);

    exitButton_.setSprite(sprite_xcircle_100,
                          SPRITE_XCIRCLE_100_WIDTH,
                          SPRITE_XCIRCLE_100_HEIGHT,
                          SPRITE_XCIRCLE_100_BYTES);
    exitButton_.setCallback(exitThunk);
    addItem(exitButton_);

    bluetoothIcon_.setSprite(sprite_bluetooth_50,
                             SPRITE_BLUETOOTH_50_WIDTH,
                             SPRITE_BLUETOOTH_50_HEIGHT,
                             SPRITE_BLUETOOTH_50_BYTES);
    addItem(bluetoothIcon_);

    answerCallButton_.setCallback(answerCallThunk);
    addItem(answerCallButton_);

    memoTypeButton_.setCallback(memoTypeThunk);
    addItem(memoTypeButton_);

    addItem(fileNameText_);
    addItem(durationText_);
    callerInfoText_.setClearOnUpdate(true);
    addItem(callerInfoText_);

    syncModeVisibility();
}

bool RecordingView::beginMemoRecording()
{
    configureMemoMode();
    startedMs_ = millis();
    durationText_.reset(startedMs_);
    lastDurationSecond_ = UINT32_MAX;
    const bool ok       = RecordingViewCallbacks::beginMemoRecording(memoTypeButton_.typeCode());
    refreshRecordingFileName();
    setDirty();
    return ok;
}

void RecordingView::configureBluetoothMode()
{
    mode_       = Mode::Bluetooth;
    frameDirty_ = true;
    syncModeVisibility();
    syncBluetoothIcon();
    setDirty();
}

void RecordingView::configureMemoMode()
{
    mode_       = Mode::Memo;
    frameDirty_ = true;
    syncModeVisibility();
    setDirty();
}

bool RecordingView::promoteMemoToBluetoothMode()
{
    if (mode_ != Mode::Memo)
    {
        return false;
    }

    configureBluetoothMode();
    callerInfoIndex_       = 0;
    nextCallerInfoCycleMs_ = millis();
    callerInfoText_.setText("");
    syncAudioButtons();
    syncAnswerCallButton();
    syncText();
    setDirty();
    return true;
}

void RecordingView::onLoad()
{
    startedMs_ = millis();
    durationText_.reset(startedMs_);
    nextCallerInfoCycleMs_ = startedMs_;
    callerInfoIndex_       = 0;
    refreshRecordingFileName();
    syncModeVisibility();
    syncBluetoothIcon();
    syncAudioButtons();
    syncAnswerCallButton();
    frameDirty_ = true;
    FlyGuiView::onLoad();
}

void RecordingView::onUnload()
{
    FlyGuiView::onUnload();
}

bool RecordingView::handleTouch(const FlyGuiTouchEvent& event)
{
    const int16_t sideTouchX = thefly_display.width() - kSideTouchSize;
    const bool    sideTouchHit =
        event.x >= sideTouchX && event.x < thefly_display.width() && event.y >= 0 && event.y < kSideTouchSize;

    if (sideTouchHit)
    {
        FlyGuiItem&      sideButton = mode_ == Mode::Bluetooth ? static_cast<FlyGuiItem&>(answerCallButton_)
                                                               : static_cast<FlyGuiItem&>(memoTypeButton_);
        FlyGuiTouchEvent sideEvent  = event;
        sideEvent.x                 = sideButton.x() + sideButton.width() / 2;
        sideEvent.y                 = sideButton.y() + sideButton.height() / 2;
        if (sideButton.handleTouch(sideEvent))
        {
            return true;
        }
    }

    return FlyGuiView::handleTouch(event);
}

void RecordingView::redraw(bool forced)
{
    syncBluetoothIcon();
    syncAudioButtons();
    syncAnswerCallButton();
    syncText();
    const bool repaintFrame = forced || frameDirty_;
    drawFrame(repaintFrame);
    FlyGuiView::redraw(repaintFrame ? true : false);
    if (repaintFrame)
    {
        frameDirty_ = false;
        markClean();
    }
    drawAudioMeters();
}

void RecordingView::onPressLeft()
{
    micButton_.trigger();
}

void RecordingView::onPressMid()
{
    speakerButton_.trigger();
}

void RecordingView::onPressRight()
{
    exitButton_.trigger();
}

void RecordingView::handleMicButton()
{
    if (AudioManager::mode() == AudioManager::P2TMode::Mic)
    {
        RecordingViewCallbacks::enableSpeakerMode();
    }
    else
    {
        RecordingViewCallbacks::setSpeakerMuted(false);
        RecordingViewCallbacks::enableMicMode();
    }
    syncAudioButtons();
}

void RecordingView::handleSpeakerButton()
{
    if (mode_ != Mode::Bluetooth || AudioManager::mode() != AudioManager::P2TMode::Speaker)
    {
        return;
    }

    RecordingViewCallbacks::toggleSpeakerMuted();
    syncAudioButtons();
}

void RecordingView::handleExitButton()
{
    FlyGui::quickScreenFade();

    char fileName[80] = {};
    RecordingViewCallbacks::stopRecording(mode_ == Mode::Bluetooth);
    const char* name = path_basename(AudioFileRecorder::currentSdPath());
    strncpy(fileName, name && name[0] != '\0' ? name : "Recording", sizeof(fileName) - 1);
    fileName[sizeof(fileName) - 1] = '\0';
    if (gui())
    {
        ModalDialog* dialog = get_view_modal_dialog();
        if (dialog)
        {
            char           message[160];
            const uint32_t overflowEvents = AudioFileRecorder::fifoOverflowEvents();
            if (overflowEvents > 0)
            {
                snprintf(message,
                         sizeof(message),
                         "%s\nhas been recorded\n%lu FIFO overflow event%s",
                         fileName,
                         static_cast<unsigned long>(overflowEvents),
                         overflowEvents == 1 ? "" : "s");
            }
            else
            {
                snprintf(message, sizeof(message), "%s\nhas been recorded", fileName);
            }
            dialog->configure(sprite_thumbsup_100,
                              SPRITE_THUMBSUP_100_BYTES,
                              SPRITE_THUMBSUP_100_WIDTH,
                              SPRITE_THUMBSUP_100_HEIGHT,
                              message,
                              FLYGUI_VIEW_MAIN);
            gui()->showView(FLYGUI_VIEW_MODAL_DIALOG);
        }
        else
        {
            gui()->showView(FLYGUI_VIEW_MAIN);
        }
    }
}

namespace
{

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

const char* path_basename(const char* path)
{
    if (!path || path[0] == '\0')
    {
        return "";
    }

    const char* name = path;
    for (const char* cursor = path; *cursor; ++cursor)
    {
        if (*cursor == '/' || *cursor == '\\')
        {
            name = cursor + 1;
        }
    }

    return name;
}

} // namespace

void RecordingView::handleAnswerCallButton()
{
    if (mode_ == Mode::Bluetooth)
    {
        RecordingViewCallbacks::answerCall();
    }
}

void RecordingView::handleMemoTypeButton()
{
    if (mode_ != Mode::Memo)
    {
        return;
    }

    memoTypeButton_.cycleNext();
    AudioFileRecorder::setMemoType(memoTypeButton_.memoType());
}

void RecordingView::micThunk(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    if (activeInstance_)
    {
        activeInstance_->handleMicButton();
    }
}

void RecordingView::speakerThunk(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    if (activeInstance_)
    {
        activeInstance_->handleSpeakerButton();
    }
}

void RecordingView::exitThunk(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    if (activeInstance_)
    {
        activeInstance_->handleExitButton();
    }
}

void RecordingView::answerCallThunk(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    if (activeInstance_)
    {
        activeInstance_->handleAnswerCallButton();
    }
}

void RecordingView::memoTypeThunk(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    if (activeInstance_)
    {
        activeInstance_->handleMemoTypeButton();
    }
}

void RecordingView::syncModeVisibility()
{
    const bool bluetoothMode = mode_ == Mode::Bluetooth;
    speakerButton_.setVisible(bluetoothMode);
    bluetoothIcon_.setVisible(bluetoothMode);
    answerCallButton_.setVisible(bluetoothMode);
    memoTypeButton_.setVisible(!bluetoothMode);
}

void RecordingView::syncBluetoothIcon()
{
    if (mode_ != Mode::Bluetooth)
    {
        return;
    }

    const BtManager::State btState = BtManager::state();
    const bool connected = btState == BtManager::State::Connected || btState == BtManager::State::AudioAvailable;
    if (bluetoothIconConnected_ == connected)
    {
        return;
    }

    bluetoothIconConnected_ = connected;
    if (connected)
    {
        bluetoothIcon_.setSprite(sprite_bluetooth_50,
                                 SPRITE_BLUETOOTH_50_WIDTH,
                                 SPRITE_BLUETOOTH_50_HEIGHT,
                                 SPRITE_BLUETOOTH_50_BYTES);
    }
    else
    {
        bluetoothIcon_.setSprite(sprite_bluetooth_x_50,
                                 SPRITE_BLUETOOTH_X_50_WIDTH,
                                 SPRITE_BLUETOOTH_X_50_HEIGHT,
                                 SPRITE_BLUETOOTH_X_50_BYTES);
    }
}

void RecordingView::syncAudioButtons()
{
    const bool micMode = AudioManager::mode() == AudioManager::P2TMode::Mic;
    micButton_.setMicMode(micMode);
    speakerButton_.setMicMode(micMode);
    speakerButton_.setMutedOverlay(!micMode && RecordingViewCallbacks::speakerMuted());
}

void RecordingView::syncAnswerCallButton()
{
    if (mode_ == Mode::Bluetooth)
    {
        answerCallButton_.setPhoneUiState(CallManager::uiState());
    }
}

void RecordingView::syncText()
{
    const uint32_t elapsedSecond = durationText_.elapsedMs() / 1000;
    if (elapsedSecond != lastDurationSecond_)
    {
        lastDurationSecond_ = elapsedSecond;
        durationText_.setDirty();
    }

    if (mode_ != Mode::Bluetooth)
    {
        callerInfoText_.setText("");
        return;
    }

    const uint32_t now             = millis();
    const size_t   callerInfoCount = CallManager::getCallerInfoCnt();
    if (callerInfoCount == 0)
    {
        callerInfoIndex_ = 0;
        callerInfoText_.setText("");
        return;
    }

    if (callerInfoIndex_ >= callerInfoCount)
    {
        callerInfoIndex_ = 0;
    }

    if (static_cast<int32_t>(now - nextCallerInfoCycleMs_) >= 0)
    {
        const char* info = CallManager::getCallerInfoAt(callerInfoIndex_);
        callerInfoText_.setText(info ? info : "");
        callerInfoIndex_       = (callerInfoIndex_ + 1) % callerInfoCount;
        nextCallerInfoCycleMs_ = now + kCallerInfoCycleMs;
    }
}

void RecordingView::refreshRecordingFileName()
{
    const char* path = AudioFileRecorder::currentSdPath();
    const char* name = path_basename(path);
    fileNameText_.setText(name && name[0] != '\0' ? name : "No recording file");
}

void RecordingView::drawFrame(bool forced)
{
    if (!forced)
    {
        return;
    }

    const int16_t screenW = thefly_display.width();
    const int16_t screenH = thefly_display.height();
    thefly_display.fillRect(0, kBorderY, screenW, screenH - kBorderY, TFT_BLACK);
    thefly_display.fillRect(kBorderX, kBorderY, screenW, kBorderSize, TFT_RED);
    thefly_display.fillRect(kBorderX, kBorderY, kBorderSize, screenH - kBorderY, TFT_RED);
    thefly_display.fillRect(screenW - kBorderSize, kBorderY, kBorderSize, screenH - kBorderY, TFT_RED);
    thefly_display.fillRect(kBorderX, screenH - kBorderSize, screenW, kBorderSize, TFT_RED);
}

void RecordingView::drawAudioMeters()
{
    micButton_.drawAudioMeter();
    if (mode_ == Mode::Bluetooth)
    {
        speakerButton_.drawAudioMeter();
    }
}
