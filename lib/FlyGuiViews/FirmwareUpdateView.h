#pragma once

#include "../FlyGui/FlyGui.h"
#include "../FlyGui/ProgressBar.h"
#include "FirmwareUpdate.h"

class FirmwareUpdateView : public FlyGuiView
{
public:
    FirmwareUpdateView();

    void configure(bool batteryFullish);
    bool dismissed() const
    {
        return dismissed_;
    }

    void onLoad() override;
    void onUnload() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    void redraw(bool forced) override;
    void onPressLeft() override;
    void onPressRight() override;

private:
    enum class State : uint8_t
    {
        Prompt,
        Updating,
        Complete,
    };

    static void onGoTriggered(uint32_t pressDurationMs);
    static void onCancelTriggered(uint32_t pressDurationMs);
    static void onProgress(uint64_t bytesWritten, uint64_t bytesTotal);

    void cancel();
    void startUpdate();
    void updateProgress(uint64_t bytesWritten, uint64_t bytesTotal);
    void finishUpdate(MicroSdCard::FirmwareUpdateResult result);

    void drawPrompt();
    void drawUpdating();
    void drawComplete();
    void drawTopSprite(const uint8_t* data, size_t bytes, uint32_t width, uint32_t height);
    void drawMessage(const char* text, int16_t maxY);
    void drawHourglassFrame(uint8_t frame);
    void drawProgress();

    float   progressPercent() const;
    uint8_t roundedProgressPercent() const;
    const char* failureText() const;

    State      state_ = State::Prompt;
    bool       batteryFullish_ = false;
    bool       dismissed_ = false;
    uint64_t   bytesWritten_ = 0;
    uint64_t   bytesTotal_ = 0;
    uint8_t    hourglassFrame_ = 0;
    MicroSdCard::FirmwareUpdateResult result_ = MicroSdCard::FirmwareUpdateResult::Ok;
    FlyGuiItem goItem_;
    FlyGuiItem cancelItem_;
    ProgressBar progressBar_;

    static FirmwareUpdateView* activeView_;
};
