#include "ScrubBar.h"

#include <stdio.h>

ScrubBar::ScrubBar(int16_t x, int16_t y, int16_t width, int16_t height, uint32_t totalMs)
    : FlyGuiItem(x, y, width, height),
      endMs_(totalMs),
      hitboxEndOffsetY_(height)
{
    setTouchable(true);
}

void ScrubBar::setTotalMs(uint32_t totalMs)
{
    setRangeMs(0, totalMs);
}

void ScrubBar::setRangeMs(uint32_t startMs, uint32_t endMs)
{
    if (endMs < startMs)
    {
        endMs = startMs;
    }

    if (startMs_ == startMs && endMs_ == endMs)
    {
        return;
    }

    startMs_    = startMs;
    endMs_      = endMs;
    positionMs_ = clampPosition(positionMs_);
    drawn_      = false;
    lastTextPositionSecond_ = UINT32_MAX;
    lastTextEndSecond_      = UINT32_MAX;
    setDirty();
    if (owner())
    {
        owner()->setDirty();
    }
}

void ScrubBar::setPositionMs(uint32_t positionMs)
{
    const uint32_t next = clampPosition(positionMs);
    if (next == positionMs_ && drawn_ && !dirty())
    {
        return;
    }

    positionMs_ = next;
    if (!drawn_ || dirty())
    {
        setDirty();
        if (owner())
        {
            owner()->setDirty();
        }
        return;
    }

    drawFill();
    drawText(false);
}

void ScrubBar::setColors(uint16_t fillColor, uint16_t emptyColor, uint16_t frameColor)
{
    if (fillColor_ == fillColor && emptyColor_ == emptyColor && frameColor_ == frameColor)
    {
        return;
    }

    fillColor_  = fillColor;
    emptyColor_ = emptyColor;
    frameColor_ = frameColor;
    drawn_      = false;
    setDirty();
    if (owner())
    {
        owner()->setDirty();
    }
}

void ScrubBar::setHitboxYOffsets(int16_t startOffsetY, int16_t endOffsetY)
{
    if (endOffsetY < startOffsetY)
    {
        endOffsetY = startOffsetY;
    }

    hitboxStartOffsetY_ = startOffsetY;
    hitboxEndOffsetY_   = endOffsetY;
}

void ScrubBar::setShowText(bool showText)
{
    if (showText_ == showText)
    {
        return;
    }

    showText_ = showText;
    lastTextPositionSecond_ = UINT32_MAX;
    lastTextEndSecond_      = UINT32_MAX;
    setDirty();
    if (owner())
    {
        owner()->setDirty();
    }
}

void ScrubBar::setScrubCallback(ScrubBarCallback callback, void* context)
{
    callback_        = callback;
    callbackContext_ = context;
    setTouchable(callback_ != nullptr);
}

void ScrubBar::onLoad()
{
    drawn_ = false;
    FlyGuiItem::onLoad();
}

bool ScrubBar::handleTouch(const FlyGuiTouchEvent& event)
{
    if (!visible() || !touchable())
    {
        return false;
    }

    const bool hit = hitTest(event.x, event.y);
    if (!hit)
    {
        return false;
    }

    if (event.justPressed || event.pressed || event.justReleased)
    {
        const uint32_t next = positionForTouchX(event.x);
        setPositionMs(next);
        if (callback_)
        {
            callback_(next, callbackContext_);
        }
        return true;
    }

    return false;
}

void ScrubBar::redraw(bool forced)
{
    if (!visible() || (!forced && !dirty()))
    {
        return;
    }

    firstDraw();
}

uint32_t ScrubBar::clampPosition(uint32_t positionMs) const
{
    if (positionMs < startMs_)
    {
        return startMs_;
    }
    if (positionMs > endMs_)
    {
        return endMs_;
    }

    return positionMs;
}

uint32_t ScrubBar::positionForTouchX(int16_t touchX) const
{
    const uint32_t rangeMs = endMs_ > startMs_ ? endMs_ - startMs_ : 0;
    if (rangeMs == 0 || width() <= 2)
    {
        return startMs_;
    }

    const int16_t innerX     = static_cast<int16_t>(x() + 1);
    const int16_t innerWidth = static_cast<int16_t>(width() - 2);
    int16_t       offset     = static_cast<int16_t>(touchX - innerX);
    if (offset < 0)
    {
        offset = 0;
    }
    if (offset > innerWidth)
    {
        offset = innerWidth;
    }

    return startMs_ + static_cast<uint32_t>((static_cast<uint64_t>(offset) * rangeMs) / innerWidth);
}

bool ScrubBar::hitTest(int16_t touchX, int16_t touchY) const
{
    const int16_t left   = x();
    const int16_t right  = static_cast<int16_t>(x() + width());
    const int16_t top    = static_cast<int16_t>(y() + hitboxStartOffsetY_);
    const int16_t bottom = static_cast<int16_t>(y() + hitboxEndOffsetY_);
    return touchX >= left && touchX < right && touchY >= top && touchY < bottom;
}

void ScrubBar::firstDraw()
{
    if (!visible())
    {
        return;
    }

    drawFrame();
    drawFill();
    drawText(true);
    drawn_ = true;
    markClean();
}

void ScrubBar::drawFrame() const
{
    if (width() <= 0 || height() <= 0)
    {
        return;
    }

    thefly_display.drawFastHLine(x(), y(), width(), frameColor_);
    thefly_display.drawFastHLine(x(), static_cast<int16_t>(y() + height() - 1), width(), frameColor_);
    thefly_display.drawFastVLine(x(), y(), height(), frameColor_);
    thefly_display.drawFastVLine(static_cast<int16_t>(x() + width() - 1), y(), height(), frameColor_);
}

void ScrubBar::drawFill()
{
    if (width() <= 2 || height() <= 2)
    {
        return;
    }

    const int16_t innerX      = static_cast<int16_t>(x() + 1);
    const int16_t innerY      = static_cast<int16_t>(y() + 1);
    const int16_t innerWidth  = static_cast<int16_t>(width() - 2);
    const int16_t innerHeight = static_cast<int16_t>(height() - 2);
    const int16_t filled      = filledWidth();
    const int16_t empty       = static_cast<int16_t>(innerWidth - filled);

    if (filled > 0)
    {
        thefly_display.fillRect(innerX, innerY, filled, innerHeight, fillColor_);
    }
    if (empty > 0)
    {
        thefly_display.fillRect(static_cast<int16_t>(innerX + filled), innerY, empty, innerHeight, emptyColor_);
    }
}

void ScrubBar::drawText(bool forced)
{
    if (!showText_)
    {
        return;
    }

    const uint32_t positionSecond = positionMs_ / 1000;
    const uint32_t endSecond      = endMs_ / 1000;
    if (!forced && positionSecond == lastTextPositionSecond_ && endSecond == lastTextEndSecond_)
    {
        return;
    }

    lastTextPositionSecond_ = positionSecond;
    lastTextEndSecond_      = endSecond;

    char currentText[13];
    char endText[13];
    formatTime(positionMs_, currentText, sizeof(currentText));
    formatTime(endMs_, endText, sizeof(endText));

    const int16_t screenW = thefly_display.width();
    const int16_t totalY = static_cast<int16_t>(y() - 17);
    thefly_display.fillRect(static_cast<int16_t>(screenW - 96), totalY, 94, 16, TFT_BLACK);
    thefly_display.setTextDatum(top_right);
    thefly_display.setTextFont(2);
    thefly_display.setTextSize(1.0f);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);
    thefly_display.drawString(endText, static_cast<int16_t>(screenW - 4), totalY);

    const int16_t currentX = 4;
    const int16_t currentY = static_cast<int16_t>(y() + height() + 5);
    thefly_display.fillRect(currentX, currentY, 150, 30, TFT_BLACK);
    thefly_display.setTextDatum(top_left);
    thefly_display.setTextFont(4);
    thefly_display.setTextSize(1.0f);
    thefly_display.setTextColor(TFT_YELLOW, TFT_BLACK);
    thefly_display.drawString(currentText, currentX, currentY);
}

void ScrubBar::formatTime(uint32_t ms, char* out, size_t outSize) const
{
    if (!out || outSize == 0)
    {
        return;
    }

    const uint32_t totalSeconds = ms / 1000;
    const uint32_t hours        = totalSeconds / 3600;
    const uint32_t minutes      = (totalSeconds / 60) % 60;
    const uint32_t seconds      = totalSeconds % 60;
    snprintf(out,
             outSize,
             "%02lu:%02lu:%02lu",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
}

int16_t ScrubBar::filledWidth() const
{
    const int16_t innerWidth = width() > 2 ? static_cast<int16_t>(width() - 2) : 0;
    const uint32_t rangeMs = endMs_ > startMs_ ? endMs_ - startMs_ : 0;
    if (rangeMs == 0 || positionMs_ <= startMs_)
    {
        return 0;
    }
    if (positionMs_ >= endMs_)
    {
        return innerWidth;
    }

    const uint32_t offsetMs = positionMs_ - startMs_;
    return static_cast<int16_t>((static_cast<uint64_t>(innerWidth) * offsetMs) / rangeMs);
}
