#include "ModalDialog.h"

#include "FlyGuiText.h"
#include "SpriteDraw.h"
#include <string.h>

namespace
{
constexpr int16_t kSpriteY        = FlyGui::topBarHeight();
constexpr int16_t kTextX          = 16;
constexpr int16_t kTextY          = 130;
constexpr int16_t kTextWidth      = 288;
constexpr int16_t kTextMaxY       = 212;
constexpr int16_t kTextLineHeight = 16;
constexpr int16_t kFooterY        = 224;
constexpr float   kTextSize       = 1.5f;
constexpr uint8_t kTextFont       = 1;
} // namespace

ModalDialog::ModalDialog() : FlyGuiView(FLYGUI_VIEW_MODAL_DIALOG)
{
}

void ModalDialog::configure(const uint8_t* spriteData,
                            size_t spriteBytes,
                            uint32_t spriteWidth,
                            uint32_t spriteHeight,
                            const char* text,
                            uint16_t nextViewId)
{
    spriteData_   = spriteData;
    spriteBytes_  = spriteBytes;
    spriteWidth_  = spriteWidth;
    spriteHeight_ = spriteHeight;
    nextViewId_   = nextViewId;
    configured_   = true;

    strncpy(text_, text ? text : "", sizeof(text_) - 1);
    text_[sizeof(text_) - 1] = '\0';
    setDirty();
}

void ModalDialog::onLoad()
{
    FlyGuiView::onLoad();
    setDirty();
}

void ModalDialog::onUnload()
{
    clearConfiguration();
    FlyGuiView::onUnload();
}

bool ModalDialog::handleTouch(const FlyGuiTouchEvent& event)
{
    if (event.justPressed || event.justReleased)
    {
        dismiss();
    }

    return true;
}

void ModalDialog::redraw(bool forced)
{
    if (!configured_ || (!forced && !dirty()))
    {
        return;
    }

    thefly_display.fillRect(0,
                            FlyGui::topBarHeight(),
                            thefly_display.width(),
                            static_cast<int16_t>(thefly_display.height() - FlyGui::topBarHeight()),
                            TFT_BLACK);
    drawSprite();
    drawText();
    drawFooter();
    markClean();
}

void ModalDialog::onPressLeft()
{
    dismiss();
}

void ModalDialog::onPressMid()
{
    dismiss();
}

void ModalDialog::onPressRight()
{
    dismiss();
}

void ModalDialog::dismiss()
{
    FlyGui* owner = gui();
    if (!owner)
    {
        return;
    }

    const uint16_t nextViewId = nextViewId_;
    owner->showView(nextViewId);
}

void ModalDialog::clearConfiguration()
{
    spriteData_   = nullptr;
    spriteBytes_  = 0;
    spriteWidth_  = 0;
    spriteHeight_ = 0;
    text_[0]      = '\0';
    nextViewId_   = FLYGUI_VIEW_MAIN;
    configured_   = false;
    setDirty();
}

void ModalDialog::drawSprite()
{
    if (!spriteData_ || spriteBytes_ == 0 || spriteWidth_ == 0 || spriteHeight_ == 0)
    {
        return;
    }

    const int32_t x = (thefly_display.width() - static_cast<int32_t>(spriteWidth_)) / 2;
    SpriteDraw::drawPng(spriteData_, spriteBytes_, x, kSpriteY, spriteWidth_, spriteHeight_, true);
}

void ModalDialog::drawText()
{
    if (text_[0] == '\0')
    {
        return;
    }

    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextDatum(top_left);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);
    FlyGuiTextUtil::drawWrappedText(text_, kTextX, kTextY, kTextWidth, kTextMaxY, kTextLineHeight);
}

void ModalDialog::drawFooter()
{
    static constexpr const char* kFooter = "press anywhere to continue";

    thefly_display.setTextFont(1);
    thefly_display.setTextSize(1.0f);
    thefly_display.setTextDatum(top_center);
    thefly_display.setTextColor(TFT_YELLOW, TFT_BLACK);
    thefly_display.drawString(kFooter, thefly_display.width() / 2, kFooterY);
}
