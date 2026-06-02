#include "PinPadView.h"

#include <Arduino.h>
#include <string.h>

namespace
{
constexpr int16_t  kLineTopGapPx              = 2;
constexpr int16_t  kLineHeightPx              = 2;
constexpr int16_t  kSegmentTickWidthPx        = 3;
constexpr int16_t  kGridTopGapPx              = 2;
constexpr int16_t  kGridBottomGapPx           = 2;
constexpr int16_t  kPinPadYOffsetPx           = 1;
constexpr uint8_t  kBottomFont                = 2;
constexpr uint8_t  kDigitFont                 = 4;
constexpr float    kBottomTextSize            = 1.0f;
constexpr float    kDigitTextSize             = 2.0f;
constexpr uint32_t kShortCooldownMs           = 2000;
constexpr uint32_t kLongCooldownMs            = 5000;
constexpr uint32_t kShortCooldownAttemptCount = 3;
constexpr size_t   kObscuredShortPinLength    = 6;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static bool    obscure_pin_length(size_t pinLength);
static size_t  display_length_for_pin(size_t pinLength);
static int16_t line_y();
static int16_t line_bottom();
static int16_t bottom_text_top();
static int16_t grid_top();
static int16_t grid_bottom();
static int16_t proportional_width(int16_t fullWidth, size_t numerator, size_t denominator);
} // namespace

// -----------------------------------------------------------------------------
// Pin Pad View
// -----------------------------------------------------------------------------

PinPadView::PinPadView(uint16_t viewId) : FlyGuiView(viewId)
{
    for (uint8_t i = 0; i < 9; ++i)
    {
        buttons_[i].configure(this, static_cast<uint8_t>(i + 1));
    }
}

void PinPadView::configure(PinPadSuccessCallback onSuccess,
                           PinPadFailedCallback  onFailedAttempt,
                           PinPadExitCallback    onExit)
{
    onSuccess_          = onSuccess;
    onFailedAttempt_    = onFailedAttempt;
    onExit_             = onExit;
    cooldownActive_     = false;
    cooldownLinePrimed_ = false;
    resetEntry();
}

void PinPadView::onLoad()
{
    FlyGuiView::onLoad();
    cooldownActive_     = false;
    cooldownLinePrimed_ = false;
    setButtonsDimmed(false);
    resetEntry();
    releaseButtons();
    layoutButtons();
    setDirty();
}

bool PinPadView::handleTouch(const FlyGuiTouchEvent& event)
{
    if (cooldownActive_)
    {
        return true;
    }

    for (PinNumBtn& button : buttons_)
    {
        if (button.handleTouch(event))
        {
            return true;
        }
    }

    return false;
}

void PinPadView::redraw(bool forced)
{
    const bool needsRedraw = forced || dirty() || lineDirty_ || cooldownActive_ || anyButtonDirty();
    if (!needsRedraw)
    {
        return;
    }

    const uint32_t now        = millis();
    const bool     fullRedraw = forced || dirty();

    if (fullRedraw)
    {
        layoutButtons();
    }

    if (fullRedraw)
    {
        thefly_display.fillRect(0,
                                FlyGui::topBarHeight(),
                                thefly_display.width(),
                                static_cast<int16_t>(thefly_display.height() - FlyGui::topBarHeight()),
                                TFT_BLACK);
        drawBottomButtons();
        if (cooldownActive_)
        {
            cooldownLinePrimed_ = false;
            cooldownLastWidth_  = thefly_display.width();
        }
    }

    for (PinNumBtn& button : buttons_)
    {
        button.redraw(fullRedraw);
    }

    if (cooldownActive_)
    {
        drawCooldownLine(now);
    }
    else if (lineDirty_ || fullRedraw)
    {
        drawProgressLine();
    }

    if (fullRedraw)
    {
        markClean();
    }
}

void PinPadView::onPressLeft()
{
    if (cooldownActive_)
    {
        return;
    }

    resetEntry();
}

void PinPadView::onPressRight()
{
    if (cooldownActive_)
    {
        return;
    }

    releaseButtons();
    if (onExit_)
    {
        onExit_();
    }
}

void PinPadView::registerDigit(uint8_t digit)
{
    if (cooldownActive_)
    {
        return;
    }

    const size_t pinLength     = targetLength();
    const size_t displayLength = display_length_for_pin(pinLength);
    if (entryLength_ >= displayLength || entryLength_ >= kMaxPinLength)
    {
        return;
    }

    entry_[entryLength_++] = static_cast<char>('0' + digit);
    entry_[entryLength_]   = '\0';
    lineDirty_             = true;

    if (hiddenFailurePending_)
    {
        if (entryLength_ >= displayLength)
        {
            startFailureCooldown();
        }
        return;
    }

    if (pinLength == 0)
    {
        hiddenFailurePending_ = true;
        return;
    }

    if (entryLength_ < pinLength)
    {
        return;
    }

    const char* targetPin = PinCode::getPin();
    if (targetPin && strncmp(entry_, targetPin, pinLength) == 0)
    {
        if (!PinCode::logSuccessfulUsage())
        {
            DBG_LOGW("PinPadView", "failed to log successful PIN usage");
        }
        resetEntry();
        releaseButtons();
        if (onSuccess_)
        {
            onSuccess_();
        }
        return;
    }

    if (obscure_pin_length(pinLength))
    {
        hiddenFailurePending_ = true;
        return;
    }

    startFailureCooldown();
}

void PinPadView::resetEntry()
{
    entryLength_          = 0;
    entry_[0]             = '\0';
    hiddenFailurePending_ = false;
    lineDirty_            = true;
}

void PinPadView::startFailureCooldown()
{
    releaseButtons();
    uint32_t failedAttempts = 0;
    if (!PinCode::logBadAttempt(&failedAttempts))
    {
        DBG_LOGW("PinPadView", "failed to log bad PIN attempt");
        failedAttempts = 1;
    }
    if (onFailedAttempt_)
    {
        onFailedAttempt_(failedAttempts);
    }

    cooldownActive_     = true;
    cooldownLinePrimed_ = false;
    cooldownLastWidth_  = thefly_display.width();
    cooldownStartedMs_  = millis();
    cooldownDurationMs_ = failedAttempts <= kShortCooldownAttemptCount ? kShortCooldownMs : kLongCooldownMs;
    setButtonsDimmed(true);
}

void PinPadView::drawBottomButtons()
{
    const int16_t screenW = thefly_display.width();
    const int16_t labelY  = bottom_text_top();

    thefly_display.setTextDatum(top_center);
    thefly_display.setTextFont(kBottomFont);
    thefly_display.setTextSize(kBottomTextSize);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);
    thefly_display.drawString("CLEAR", screenW / 6, labelY);
    thefly_display.drawString("EXIT", static_cast<int16_t>((screenW * 5) / 6), labelY);
}

void PinPadView::drawProgressLine()
{
    const int16_t screenW       = thefly_display.width();
    const size_t  pinLength     = targetLength();
    const size_t  displayLength = display_length_for_pin(pinLength);
    const int16_t width         = proportional_width(screenW, entryLength_, displayLength);

    thefly_display.fillRect(0, line_y(), screenW, kLineHeightPx, TFT_BLACK);
    if (width > 0)
    {
        thefly_display.fillRect(0, line_y(), width, kLineHeightPx, TFT_YELLOW);
        drawSegmentTicks(width);
    }
    lineDirty_ = false;
}

void PinPadView::drawCooldownLine(uint32_t now)
{
    const int16_t  screenW = thefly_display.width();
    const uint32_t elapsed = now - cooldownStartedMs_;

    if (elapsed >= cooldownDurationMs_ || cooldownDurationMs_ == 0)
    {
        if (cooldownLastWidth_ > 0)
        {
            thefly_display.fillRect(0, line_y(), cooldownLastWidth_, kLineHeightPx, TFT_BLACK);
        }
        cooldownActive_       = false;
        cooldownLinePrimed_   = false;
        cooldownLastWidth_    = 0;
        entryLength_          = 0;
        entry_[0]             = '\0';
        hiddenFailurePending_ = false;
        lineDirty_            = false;
        setButtonsDimmed(false);
        return;
    }

    if (!cooldownLinePrimed_)
    {
        thefly_display.fillRect(0, line_y(), screenW, kLineHeightPx, TFT_RED);
        drawSegmentTicks(screenW);
        cooldownLastWidth_  = screenW;
        cooldownLinePrimed_ = true;
        lineDirty_          = false;
        return;
    }

    const uint32_t remainingMs    = cooldownDurationMs_ - elapsed;
    int16_t        remainingWidth = static_cast<int16_t>(
        (static_cast<uint32_t>(screenW) * remainingMs + cooldownDurationMs_ - 1) / cooldownDurationMs_);

    if (remainingWidth < 0)
    {
        remainingWidth = 0;
    }
    if (remainingWidth > screenW)
    {
        remainingWidth = screenW;
    }

    if (remainingWidth < cooldownLastWidth_)
    {
        thefly_display.fillRect(remainingWidth,
                                line_y(),
                                static_cast<int16_t>(cooldownLastWidth_ - remainingWidth),
                                kLineHeightPx,
                                TFT_BLACK);
        cooldownLastWidth_ = remainingWidth;
    }
}

void PinPadView::drawSegmentTicks(int16_t width)
{
    const int16_t screenW   = thefly_display.width();
    const size_t  pinLength = display_length_for_pin(targetLength());

    if (screenW <= 0 || width <= 0 || pinLength <= 1)
    {
        return;
    }

    for (size_t i = 1; i < pinLength; ++i)
    {
        const int16_t x = static_cast<int16_t>((static_cast<int32_t>(screenW) * static_cast<int32_t>(i)) /
                                               static_cast<int32_t>(pinLength));
        if (x > 0 && x < width)
        {
            int16_t tickX = static_cast<int16_t>(x - (kSegmentTickWidthPx / 2));
            int16_t tickW = kSegmentTickWidthPx;
            if (tickX < 0)
            {
                tickW = static_cast<int16_t>(tickW + tickX);
                tickX = 0;
            }
            if (tickX + tickW > width)
            {
                tickW = static_cast<int16_t>(width - tickX);
            }
            if (tickW > 0)
            {
                thefly_display.fillRect(tickX, line_y(), tickW, kLineHeightPx, TFT_BLACK);
            }
        }
    }
}

void PinPadView::layoutButtons()
{
    const int16_t screenW = thefly_display.width();
    const int16_t top     = grid_top();
    const int16_t bottom  = grid_bottom();
    const int16_t gridH   = static_cast<int16_t>(bottom - top);

    if (screenW <= 0 || gridH <= 0)
    {
        return;
    }

    for (uint8_t row = 0; row < 3; ++row)
    {
        const int16_t y0 = static_cast<int16_t>(top + ((gridH * row) / 3));
        const int16_t y1 = static_cast<int16_t>(top + ((gridH * (row + 1)) / 3));
        for (uint8_t col = 0; col < 3; ++col)
        {
            const int16_t x0    = static_cast<int16_t>((screenW * col) / 3);
            const int16_t x1    = static_cast<int16_t>((screenW * (col + 1)) / 3);
            const uint8_t index = static_cast<uint8_t>((row * 3) + col);
            buttons_[index].setBounds(x0, y0, static_cast<int16_t>(x1 - x0), static_cast<int16_t>(y1 - y0));
        }
    }
}

void PinPadView::releaseButtons()
{
    for (PinNumBtn& button : buttons_)
    {
        button.release();
    }
}

void PinPadView::setButtonsDimmed(bool dimmed)
{
    for (PinNumBtn& button : buttons_)
    {
        button.setDimmed(dimmed);
    }
}

size_t PinPadView::targetLength() const
{
    const char* targetPin = PinCode::getPin();
    if (!targetPin)
    {
        return 0;
    }

    size_t length = 0;
    while (length < kMaxPinLength && targetPin[length] != '\0')
    {
        length++;
    }
    return length;
}

bool PinPadView::anyButtonDirty() const
{
    for (const PinNumBtn& button : buttons_)
    {
        if (button.dirty())
        {
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// Pin Number Button
// -----------------------------------------------------------------------------

PinNumBtn::PinNumBtn() : FlyGuiView(FLYGUI_VIEW_PIN_PAD) {}

void PinNumBtn::configure(PinPadView* owner, uint8_t number)
{
    owner_  = owner;
    number_ = number;
    setDirty();
}

void PinNumBtn::setBounds(int16_t x, int16_t y, int16_t width, int16_t height)
{
    if (x_ == x && y_ == y && width_ == width && height_ == height)
    {
        return;
    }

    x_      = x;
    y_      = y;
    width_  = width;
    height_ = height;
    setDirty();
}

void PinNumBtn::release()
{
    if (!pressed_)
    {
        return;
    }

    pressed_ = false;
    setDirty();
}

void PinNumBtn::setDimmed(bool dimmed)
{
    if (dimmed_ == dimmed)
    {
        return;
    }

    dimmed_ = dimmed;
    setDirty();
}

bool PinNumBtn::contains(int16_t x, int16_t y) const
{
    return x >= x_ && y >= y_ && x < x_ + width_ && y < y_ + height_;
}

bool PinNumBtn::handleTouch(const FlyGuiTouchEvent& event)
{
    const bool hit = contains(event.x, event.y);

    if (event.justPressed)
    {
        if (!hit)
        {
            return false;
        }

        pressed_ = true;
        setDirty();
        return true;
    }

    if (!pressed_)
    {
        return false;
    }

    if (event.justReleased || !event.pressed)
    {
        pressed_ = false;
        setDirty();
        if (hit && owner_)
        {
            owner_->registerDigit(number_);
        }
        return true;
    }

    if (!hit)
    {
        pressed_ = false;
        setDirty();
    }

    return true;
}

void PinNumBtn::redraw(bool forced)
{
    if (!forced && !dirty())
    {
        return;
    }

    if (width_ <= 0 || height_ <= 0)
    {
        markClean();
        return;
    }

    const uint16_t borderColour = pressed_ ? TFT_YELLOW : TFT_DARKGREY;
    char           label[2]     = {static_cast<char>('0' + number_), '\0'};

    thefly_display.fillRect(x_, y_, width_, height_, TFT_BLACK);
    thefly_display.drawRect(x_, y_, width_, height_, borderColour);
    thefly_display.setTextDatum(middle_center);
    thefly_display.setTextFont(kDigitFont);
    thefly_display.setTextSize(kDigitTextSize);
    thefly_display.setTextColor(dimmed_ ? 0x3186 : TFT_WHITE, TFT_BLACK);
    thefly_display.drawString(label,
                              static_cast<int16_t>(x_ + (width_ / 2)),
                              static_cast<int16_t>(y_ + (height_ / 2) + 4));
    markClean();
}

namespace
{

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

bool obscure_pin_length(size_t pinLength)
{
    return pinLength < kObscuredShortPinLength;
}

size_t display_length_for_pin(size_t pinLength)
{
    return obscure_pin_length(pinLength) ? kObscuredShortPinLength : pinLength;
}

int16_t line_y()
{
    return static_cast<int16_t>(FlyGui::topBarHeight() + kLineTopGapPx);
}

int16_t line_bottom()
{
    return static_cast<int16_t>(line_y() + kLineHeightPx);
}

int16_t bottom_text_top()
{
    thefly_display.setTextFont(kBottomFont);
    thefly_display.setTextSize(kBottomTextSize);
    return static_cast<int16_t>(thefly_display.height() - thefly_display.fontHeight() - 2);
}

int16_t grid_top()
{
    return static_cast<int16_t>(line_bottom() + kGridTopGapPx + kPinPadYOffsetPx);
}

int16_t grid_bottom()
{
    return static_cast<int16_t>(bottom_text_top() - kGridBottomGapPx);
}

int16_t proportional_width(int16_t fullWidth, size_t numerator, size_t denominator)
{
    if (fullWidth <= 0 || denominator == 0 || numerator == 0)
    {
        return 0;
    }

    if (numerator >= denominator)
    {
        return fullWidth;
    }

    return static_cast<int16_t>(
        (static_cast<int32_t>(fullWidth) * static_cast<int32_t>(numerator) + static_cast<int32_t>(denominator) - 1) /
        static_cast<int32_t>(denominator));
}

} // namespace
