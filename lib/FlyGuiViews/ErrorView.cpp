#include "ErrorView.h"

#include "FlyGuiText.h"
#include "SpriteDraw.h"
#include "sprites.h"
#include <string.h>

namespace
{
constexpr int16_t kIconY       = 24;
constexpr int16_t kTextX       = 4;
constexpr int16_t kTextY       = 140;
constexpr int16_t kTextPadding = 4;
constexpr int16_t kLineHeight  = 14;
constexpr float   kTextSize    = 1.5f;
constexpr uint8_t kTextFont    = 1;
} // namespace

ErrorView::ErrorView() : FlyGuiView(FLYGUI_VIEW_ERROR) {}

void ErrorView::setMessage(const char* message, bool fatal)
{
    strncpy(message_, message ? message : "", sizeof(message_) - 1);
    message_[sizeof(message_) - 1] = '\0';
    fatal_                         = fatal;
    dismissed_                     = false;
    setDirty();
}

void ErrorView::onLoad()
{
    dismissed_ = false;
    FlyGuiView::onLoad();
}

bool ErrorView::handleTouch(const FlyGuiTouchEvent& event)
{
    if (!fatal_ && (event.justPressed || event.justReleased))
    {
        dismissed_ = true;
        return true;
    }

    return true;
}

void ErrorView::redraw(M5GFX& display, bool forced)
{
    if (!forced && !dirty())
    {
        return;
    }

    display.fillScreen(TFT_BLACK);
    if (gui())
    {
        gui()->requestTopBarFullRedraw();
    }

    const int16_t icon_x = static_cast<int16_t>((display.width() - static_cast<int32_t>(SPRIT_ERROR_LARGE_WIDTH)) / 2);
    SpriteDraw::drawPng(display,
                        sprit_error_large,
                        SPRIT_ERROR_LARGE_BYTES,
                        icon_x,
                        kIconY,
                        SPRIT_ERROR_LARGE_WIDTH,
                        SPRIT_ERROR_LARGE_HEIGHT,
                        true,
                        nullptr);

    display.setTextFont(kTextFont);
    display.setTextSize(kTextSize);
    display.setTextDatum(top_left);
    display.setTextColor(TFT_WHITE, TFT_BLACK);

    const int16_t footer_y = static_cast<int16_t>(display.height() - kLineHeight - kTextPadding);
    FlyGuiTextUtil::drawWrappedText(display,
                                     message_,
                                     kTextX,
                                     kTextY,
                                     static_cast<int16_t>(display.width() - (kTextX * 2)),
                                     static_cast<int16_t>(footer_y - kTextPadding),
                                     kLineHeight);

    display.setTextColor(fatal_ ? TFT_RED : TFT_YELLOW, TFT_BLACK);
    display.drawString(fatal_ ? "Fatal error. Restart required." : "Touch screen to continue.", kTextX, footer_y);

    markClean();
}
