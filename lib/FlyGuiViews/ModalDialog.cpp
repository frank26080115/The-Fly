#include "ModalDialog.h"

#include "FlyGuiText.h"
#include "SpriteDraw.h"
#include <string.h>

namespace
{
constexpr int16_t kSpriteY        = FlyGui::kTopBarHeight;
constexpr int16_t kTextX          = 16;
constexpr int16_t kTextWidth      = 288;
constexpr int16_t kTextMaxY       = 212;
constexpr int16_t kTextLineHeight = 17;
constexpr int16_t kFooterY        = 224;
constexpr float   kTextSize       = 1.0f;
constexpr uint8_t kTextFont       = 2;
constexpr float   kFooterTextSize = 1.0f;
constexpr uint8_t kFooterTextFont = 2;
} // namespace

ModalDialog::ModalDialog() : FlyGuiView(FLYGUI_VIEW_MODAL_DIALOG)
{
}

void ModalDialog::configure(const uint8_t* spriteData,
                            size_t spriteBytes,
                            uint32_t spriteWidth,
                            uint32_t spriteHeight,
                            const char* text,
                            uint16_t nextViewId,
                            ModalDialogDismissCallback dismissCallback)
{
    spriteData_       = spriteData;
    spriteBytes_      = spriteBytes;
    spriteWidth_      = spriteWidth;
    spriteHeight_     = spriteHeight;
    nextViewId_       = nextViewId;
    dismissCallback_  = dismissCallback;
    configured_       = true;

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
    thefly_display.fillRect(0, FlyGui::kTopBarHeight, thefly_display.width(), thefly_display.height() - FlyGui::kTopBarHeight, TFT_BLACK);
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
                            FlyGui::kTopBarHeight,
                            thefly_display.width(),
                            static_cast<int16_t>(thefly_display.height() - FlyGui::kTopBarHeight),
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
    ModalDialogDismissCallback dismissCallback = dismissCallback_;
    owner->showView(nextViewId);
    if (dismissCallback)
    {
        dismissCallback();
    }
}

void ModalDialog::clearConfiguration()
{
    spriteData_      = nullptr;
    spriteBytes_     = 0;
    spriteWidth_     = 0;
    spriteHeight_    = 0;
    text_[0]         = '\0';
    nextViewId_      = FLYGUI_VIEW_MAIN;
    dismissCallback_ = nullptr;
    configured_      = false;
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
    const int16_t text_y = static_cast<int16_t>(kSpriteY + spriteHeight_);
    FlyGuiTextUtil::drawWrappedText(text_, kTextX, text_y, kTextWidth, kTextMaxY, kTextLineHeight);
}

void ModalDialog::drawFooter()
{
    static constexpr const char* kFooter = "press anywhere to continue";

    thefly_display.setTextFont(kFooterTextFont);
    thefly_display.setTextSize(kFooterTextSize);
    thefly_display.setTextDatum(top_center);
    thefly_display.setTextColor(TFT_YELLOW, TFT_BLACK);
    thefly_display.drawString(kFooter, thefly_display.width() / 2, kFooterY);
}
