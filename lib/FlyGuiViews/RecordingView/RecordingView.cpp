#include "RecordingView.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "AudioFileRecorder.h"
#include "AudioManager.h"
#include "CallManager.h"
#include "ModalDialog.h"
#include "RecordingViewCallbacks.h"
#include "sprites.h"

extern ModalDialog* get_modal_dialog();

namespace
{
constexpr int16_t kBorderX      = 0;
constexpr int16_t kBorderY      = FlyGui::topBarHeight();
constexpr int16_t kBorderSize   = 10;
constexpr int16_t kButtonY      = 130;
constexpr int16_t kMicButtonX   = 10;
constexpr int16_t kMidButtonX   = 110;
constexpr int16_t kExitButtonX  = 210;
constexpr int16_t kSideButtonX  = 260;
constexpr int16_t kBluetoothY   = 20;
constexpr int16_t kSideButtonY  = 70;
constexpr int16_t kTextX        = 16;
constexpr int16_t kFileTextY    = 28;
constexpr int16_t kElapsedTextY = 62;
constexpr int16_t kInfoTextY    = 96;
constexpr int16_t kTextWidth    = 240;
constexpr int16_t kTextHeight   = 28;
constexpr float   kFileTextSize = 1.0f;
constexpr uint8_t kFileTextFont = 2;
constexpr float   kDurationTextSize = 1.0f;
constexpr uint8_t kDurationTextFont = 4;
constexpr float   kCallerInfoTextSize = 1.0f;
constexpr uint8_t kCallerInfoTextFont = 2;
constexpr uint32_t kCallerInfoCycleMs = 3000;

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

RecordingView* RecordingView::activeInstance_ = nullptr;

RecordingView::RecordingView()
    : FlyGuiView(FLYGUI_VIEW_RECORDING),
      micButton_(AudioDeviceButton::Device::Mic, kMicButtonX, kButtonY),
      speakerButton_(AudioDeviceButton::Device::Speaker, kMidButtonX, kButtonY),
      exitButton_(kExitButtonX, kButtonY, SPRIT_BTN_END_WIDTH, SPRIT_BTN_END_HEIGHT),
      bluetoothIcon_(kSideButtonX, kBluetoothY, SPRIT_BLUETOOTH_50_WIDTH, SPRIT_BLUETOOTH_50_HEIGHT),
      answerCallButton_(kSideButtonX, kSideButtonY),
      memoTypeButton_(kSideButtonX, kSideButtonY),
      fileNameText_(kTextX, kFileTextY, kTextWidth, kTextHeight, kFileTextSize, kFileTextFont, 64),
      durationText_(kTextX, kElapsedTextY, kTextWidth, kTextHeight, kDurationTextSize, kDurationTextFont),
      callerInfoText_(kTextX, kInfoTextY, kTextWidth, kTextHeight, kCallerInfoTextSize, kCallerInfoTextFont, 80)
{
    activeInstance_ = this;

    micButton_.setCallback(micThunk);
    addItem(micButton_);

    speakerButton_.setCallback(speakerThunk);
    addItem(speakerButton_);

    exitButton_.setSprite(sprit_btn_end, SPRIT_BTN_END_WIDTH, SPRIT_BTN_END_HEIGHT, SPRIT_BTN_END_BYTES);
    exitButton_.setCallback(exitThunk);
    addItem(exitButton_);

    bluetoothIcon_.setSprite(sprit_bluetooth_50, SPRIT_BLUETOOTH_50_WIDTH, SPRIT_BLUETOOTH_50_HEIGHT, SPRIT_BLUETOOTH_50_BYTES);
    addItem(bluetoothIcon_);

    answerCallButton_.setCallback(answerCallThunk);
    addItem(answerCallButton_);

    memoTypeButton_.setCallback(memoTypeThunk);
    addItem(memoTypeButton_);

    addItem(fileNameText_);
    addItem(durationText_);
    addItem(callerInfoText_);

    syncModeVisibility();
}

bool RecordingView::beginBluetoothRecording()
{
    configureBluetoothMode();
    startedMs_ = millis();
    durationText_.reset(startedMs_);
    lastDurationSecond_ = UINT32_MAX;
    const bool ok = RecordingViewCallbacks::beginBluetoothRecording('C');
    refreshRecordingFileName();
    setDirty();
    return ok;
}

bool RecordingView::beginMemoRecording()
{
    configureMemoMode();
    startedMs_ = millis();
    durationText_.reset(startedMs_);
    lastDurationSecond_ = UINT32_MAX;
    const bool ok = RecordingViewCallbacks::beginMemoRecording(memoTypeButton_.typeCode());
    refreshRecordingFileName();
    setDirty();
    return ok;
}

void RecordingView::configureBluetoothMode()
{
    mode_ = Mode::Bluetooth;
    frameDirty_ = true;
    syncModeVisibility();
    setDirty();
}

void RecordingView::configureMemoMode()
{
    mode_ = Mode::Memo;
    frameDirty_ = true;
    syncModeVisibility();
    setDirty();
}

void RecordingView::onLoad()
{
    startedMs_ = millis();
    durationText_.reset(startedMs_);
    nextCallerInfoCycleMs_ = startedMs_;
    callerInfoIndex_ = 0;
    refreshRecordingFileName();
    syncModeVisibility();
    syncAudioButtons();
    syncAnswerCallButton();
    frameDirty_ = true;
    FlyGuiView::onLoad();
}

void RecordingView::onUnload()
{
    thefly_display.fillRect(0, FlyGui::topBarHeight(), thefly_display.width(), thefly_display.height() - FlyGui::topBarHeight(), TFT_BLACK);
    FlyGuiView::onUnload();
}

void RecordingView::redraw(bool forced)
{
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
    char fileName[80] = {};
    const char* name = path_basename(AudioFileRecorder::currentSdPath());
    strncpy(fileName, name && name[0] != '\0' ? name : "Recording", sizeof(fileName) - 1);
    fileName[sizeof(fileName) - 1] = '\0';

    RecordingViewCallbacks::stopRecording();
    if (gui())
    {
        ModalDialog* dialog = get_modal_dialog();
        if (dialog)
        {
            char message[128];
            snprintf(message, sizeof(message), "%s\nhas been recorded", fileName);
            dialog->configure(sprit_thumbsup_100,
                              SPRIT_THUMBSUP_100_BYTES,
                              SPRIT_THUMBSUP_100_WIDTH,
                              SPRIT_THUMBSUP_100_HEIGHT,
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
}

void RecordingView::micThunk()
{
    if (activeInstance_)
    {
        activeInstance_->handleMicButton();
    }
}

void RecordingView::speakerThunk()
{
    if (activeInstance_)
    {
        activeInstance_->handleSpeakerButton();
    }
}

void RecordingView::exitThunk()
{
    if (activeInstance_)
    {
        activeInstance_->handleExitButton();
    }
}

void RecordingView::answerCallThunk()
{
    if (activeInstance_)
    {
        activeInstance_->handleAnswerCallButton();
    }
}

void RecordingView::memoTypeThunk()
{
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

    const uint32_t now = millis();
    const size_t callerInfoCount = CallManager::getCallerInfoCnt();
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
        callerInfoIndex_ = (callerInfoIndex_ + 1) % callerInfoCount;
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
