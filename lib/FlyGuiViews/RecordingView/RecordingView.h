#pragma once

#include <stdint.h>

#include "../../FlyGui/FlyGui.h"
#include "../../FlyGui/FlyGuiText.h"
#include "AnswerCallButton.h"
#include "AudioDeviceButton.h"
#include "MemoTypeButton.h"

class RecordingView : public FlyGuiView
{
public:
    enum class Mode
    {
        Bluetooth,
        Memo,
    };

    RecordingView();

    bool beginMemoRecording();
    void configureBluetoothMode();
    void configureMemoMode();
    bool promoteMemoToBluetoothMode();

    void onLoad() override;
    void onUnload() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    bool redraw(bool forced) override;

    void onPressLeft() override;
    void onPressMid() override;
    void onPressRight() override;

    void handleMicButton();
    void handleSpeakerButton();
    void handleExitButton();
    void handleAnswerCallButton();
    void handleMemoTypeButton();

private:
    static void micThunk(uint32_t pressDurationMs);
    static void speakerThunk(uint32_t pressDurationMs);
    static void exitThunk(uint32_t pressDurationMs);
    static void answerCallThunk(uint32_t pressDurationMs);
    static void memoTypeThunk(uint32_t pressDurationMs);

    void syncModeVisibility();
    void syncBluetoothIcon();
    void syncExtCodecAudioIcons();
    void syncAudioButtons();
    void syncAnswerCallButton();
    void syncText();
    void refreshRecordingFileName();
    void drawFrame(bool forced);
    bool drawAudioMeters();
    bool fullDuplexAudio() const;

    static RecordingView* activeInstance_;

    Mode              mode_ = Mode::Bluetooth;
    AudioDeviceButton micButton_;
    AudioDeviceButton speakerButton_;
    FlyGuiItem        exitButton_;
    FlyGuiItem        bluetoothIcon_;
    AnswerCallButton  answerCallButton_;
    MemoTypeButton    memoTypeButton_;
    FlyGuiText        fileNameText_;
    FlyGuiStopwatch   durationText_;
    FlyGuiText        callerInfoText_;
    uint32_t          startedMs_              = 0;
    uint32_t          lastDurationSecond_     = UINT32_MAX;
    uint32_t          nextCallerInfoCycleMs_  = 0;
    size_t            callerInfoIndex_        = 0;
    uint32_t          extCodecStateGeneration_ = UINT32_MAX;
    bool              bluetoothIconConnected_ = true;
    bool              frameDirty_             = true;
};
