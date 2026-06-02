#pragma once

#include "thefly_common.h"

#include <stddef.h>
#include <stdint.h>

#include "../FlyGui/FlyGui.h"

using ScrubBarCallback = void (*)(uint32_t positionMs, void* context);

class ScrubBar : public FlyGuiItem
{
public:
    ScrubBar(int16_t x, int16_t y, int16_t width, int16_t height, uint32_t totalMs = 0);

    void setTotalMs(uint32_t totalMs);
    void setRangeMs(uint32_t startMs, uint32_t endMs);
    void setPositionMs(uint32_t positionMs);
    void setColors(uint16_t fillColor, uint16_t emptyColor = TFT_BLACK, uint16_t frameColor = TFT_WHITE);
    void setHitboxYOffsets(int16_t startOffsetY, int16_t endOffsetY);
    void setShowText(bool showText);
    void setScrubCallback(ScrubBarCallback callback, void* context);

    uint32_t totalMs() const
    {
        return endMs_;
    }

    uint32_t startMs() const
    {
        return startMs_;
    }

    uint32_t endMs() const
    {
        return endMs_;
    }

    uint32_t positionMs() const
    {
        return positionMs_;
    }

    void onLoad() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    void redraw(bool forced) override;

private:
    uint32_t clampPosition(uint32_t positionMs) const;
    uint32_t positionForTouchX(int16_t touchX) const;
    bool     hitTest(int16_t touchX, int16_t touchY) const;
    void     firstDraw();
    void     drawFrame() const;
    void     drawFill();
    void     drawText(bool forced);
    void     formatTime(uint32_t ms, char* out, size_t outSize) const;
    int16_t  filledWidth() const;

    uint32_t         startMs_                = 0;
    uint32_t         endMs_                  = 0;
    uint32_t         positionMs_             = 0;
    uint16_t         fillColor_              = TFT_BLUE;
    uint16_t         emptyColor_             = TFT_BLACK;
    uint16_t         frameColor_             = TFT_WHITE;
    int16_t          hitboxStartOffsetY_     = 0;
    int16_t          hitboxEndOffsetY_       = 0;
    bool             showText_               = false;
    bool             drawn_                  = false;
    uint32_t         lastTextPositionSecond_ = UINT32_MAX;
    uint32_t         lastTextEndSecond_      = UINT32_MAX;
    ScrubBarCallback callback_               = nullptr;
    void*            callbackContext_        = nullptr;
};
