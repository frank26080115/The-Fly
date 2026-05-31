#pragma once

#include "FlyGui.h"
#include "PinCode.h"

#include <stddef.h>
#include <stdint.h>

class PinPadView;

using PinPadSuccessCallback = void (*)();
using PinPadFailedCallback  = void (*)(uint32_t failedAttempts);
using PinPadExitCallback    = void (*)();

class PinNumBtn : public FlyGuiView
{
public:
    PinNumBtn();

    void configure(PinPadView* owner, uint8_t number);
    void setBounds(int16_t x, int16_t y, int16_t width, int16_t height);
    void setDimmed(bool dimmed);
    void release();

    bool handleTouch(const FlyGuiTouchEvent& event) override;
    void redraw(bool forced) override;

private:
    bool contains(int16_t x, int16_t y) const;

    PinPadView* owner_  = nullptr;
    uint8_t     number_ = 0;
    int16_t     x_      = 0;
    int16_t     y_      = 0;
    int16_t     width_  = 0;
    int16_t     height_ = 0;
    bool        pressed_ = false;
    bool        dimmed_  = false;
};

class PinPadView : public FlyGuiView
{
public:
    explicit PinPadView(uint16_t viewId = FLYGUI_VIEW_PIN_PAD);

    void configure(PinPadSuccessCallback onSuccess,
                   PinPadFailedCallback onFailedAttempt,
                   PinPadExitCallback onExit);

    void registerDigit(uint8_t digit);

    void onLoad() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    void redraw(bool forced) override;
    void onPressLeft() override;
    void onPressRight() override;

private:
    static constexpr size_t kMaxPinLength = 32;

    void resetEntry();
    void startFailureCooldown();
    void drawBottomButtons();
    void drawProgressLine();
    void drawCooldownLine(uint32_t now);
    void drawSegmentTicks(int16_t width);
    void layoutButtons();
    void releaseButtons();
    void setButtonsDimmed(bool dimmed);
    size_t targetLength() const;
    bool anyButtonDirty() const;

    char                  entry_[kMaxPinLength + 1]     = {};
    size_t                entryLength_                  = 0;
    bool                  hiddenFailurePending_          = false;
    bool                  cooldownActive_               = false;
    bool                  lineDirty_                    = false;
    bool                  cooldownLinePrimed_           = false;
    int16_t               cooldownLastWidth_            = 0;
    uint32_t              cooldownStartedMs_            = 0;
    uint32_t              cooldownDurationMs_           = 0;
    PinPadSuccessCallback onSuccess_                    = nullptr;
    PinPadFailedCallback  onFailedAttempt_              = nullptr;
    PinPadExitCallback    onExit_                       = nullptr;
    PinNumBtn             buttons_[9];
};
