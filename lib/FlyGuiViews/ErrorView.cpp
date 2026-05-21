#include "ErrorView.h"

#include "FlyGuiText.h"
#include "SpriteDraw.h"
#include "sprites.h"
#include <string.h>

namespace
{
constexpr int16_t kIconY       = FlyGui::kTopBarHeight + 20;
constexpr int16_t kTextX       = 4;
constexpr int16_t kTextGap     = 5;
constexpr int16_t kTextPadding = 4;
constexpr int16_t kTextLineHeight = 17;
constexpr int16_t kFooterLineHeight = 14;
constexpr float   kTextSize    = 1.0f;
constexpr uint8_t kTextFont    = 2;
constexpr float   kFooterTextSize = 1.0f;
constexpr uint8_t kFooterTextFont = 2;
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

void ErrorView::redraw(bool forced)
{
    if (!forced && !dirty())
    {
        return;
    }

    thefly_display.fillScreen(TFT_BLACK);
    if (gui())
    {
        gui()->requestTopBarFullRedraw();
    }

    const int16_t icon_x = static_cast<int16_t>((thefly_display.width() - static_cast<int32_t>(SPRIT_WARNING_100_WIDTH)) / 2);
    SpriteDraw::drawPng(sprit_warning_100,
                        SPRIT_WARNING_100_BYTES,
                        icon_x,
                        kIconY,
                        SPRIT_WARNING_100_WIDTH,
                        SPRIT_WARNING_100_HEIGHT,
                        true);

    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextDatum(top_left);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);

    const int16_t text_y = static_cast<int16_t>(kIconY + SPRIT_WARNING_100_HEIGHT + kTextGap);
    const int16_t footer_y = static_cast<int16_t>(thefly_display.height() - kFooterLineHeight - kTextPadding);
    FlyGuiTextUtil::drawWrappedText(message_,
                                     kTextX,
                                     text_y,
                                     static_cast<int16_t>(thefly_display.width() - (kTextX * 2)),
                                     static_cast<int16_t>(footer_y - kTextPadding),
                                     kTextLineHeight);

    thefly_display.setTextFont(kFooterTextFont);
    thefly_display.setTextSize(kFooterTextSize);
    thefly_display.setTextColor(fatal_ ? TFT_RED : TFT_YELLOW, TFT_BLACK);
    thefly_display.drawString(fatal_ ? "Fatal error. Restart required." : "Touch screen to continue.", kTextX, footer_y);

    markClean();
}
