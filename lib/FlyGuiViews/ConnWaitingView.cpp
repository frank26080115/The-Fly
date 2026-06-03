#include "ConnWaitingView.h"

#include "FlyGuiText.h"
#include "SpriteDraw.h"
#include "sprites.h"
#include <stdio.h>
#include <string.h>

namespace
{
constexpr int16_t  kContentY          = FlyGui::kTopBarHeight;
constexpr int16_t  kMainSpriteCenterY = FlyGui::kTopBarHeight + 75;
constexpr int16_t  kBottomSpriteSize  = 60;
constexpr int16_t  kBottomY           = 180;
constexpr int16_t  kHourglassX        = 0;
constexpr int16_t  kTextX             = 64;
constexpr int16_t  kTextY             = kBottomY;
constexpr int16_t  kTextWidth         = 192;
constexpr int16_t  kTextMaxY          = 238;
constexpr int16_t  kTextLineHeight    = 17;
constexpr float    kTextSize          = 1.0f;
constexpr uint8_t  kTextFont          = 2;
constexpr int16_t  kCancelX           = 260;
constexpr uint32_t kHourglassPeriodMs = 333;

struct SpriteRef
{
    const uint8_t* data   = nullptr;
    uint32_t       width  = 0;
    uint32_t       height = 0;
    size_t         bytes  = 0;
};

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static SpriteRef main_sprite_for_mode(ConnWaitingMode mode);
static SpriteRef hourglass_sprite_for_frame(uint8_t frame);
} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

ConnWaitingView::ConnWaitingView(ConnWaitingMode    mode,
                                 const char*        targetName,
                                 FlyGuiItemCallback cancelCallback,
                                 uint16_t           viewId)
    : FlyGuiView(viewId), mode_(mode), cancelItem_(kCancelX, kBottomY, kBottomSpriteSize, kBottomSpriteSize)
{
    strncpy(targetName_, targetName ? targetName : "", sizeof(targetName_) - 1);
    targetName_[sizeof(targetName_) - 1] = '\0';

    cancelItem_.setSprite(sprite_cancel_60, SPRITE_CANCEL_60_WIDTH, SPRITE_CANCEL_60_HEIGHT, SPRITE_CANCEL_60_BYTES);
    cancelItem_.setCallback(cancelCallback);
    addItem(cancelItem_);
}

void ConnWaitingView::configure(ConnWaitingMode mode, const char* targetName)
{
    mode_ = mode;
    strncpy(targetName_, targetName ? targetName : "", sizeof(targetName_) - 1);
    targetName_[sizeof(targetName_) - 1] = '\0';
    setDirty();
}

void ConnWaitingView::setCancelCallback(FlyGuiItemCallback cancelCallback)
{
    cancelItem_.setCallback(cancelCallback);
}

void ConnWaitingView::onLoad()
{
    hourglassFrame_      = 0;
    lastHourglassDrawMs_ = 0;
    FlyGuiView::onLoad();
}

bool ConnWaitingView::handleTouch(const FlyGuiTouchEvent& event)
{
    return cancelItem_.handleTouch(event);
}

bool ConnWaitingView::redraw(bool forced)
{
    const bool     redrawStatic = forced || dirty();
    const uint32_t now          = millis();

    if (redrawStatic)
    {
        drawStaticContent();
        hourglassFrame_      = 0;
        lastHourglassDrawMs_ = now;
        bool drawn           = true;
        drawn |= updateHourglass(now, true);
        drawn |= cancelItem_.redraw(true);
        markClean();
        return drawn;
    }

    return updateHourglass(now, false);
}

void ConnWaitingView::drawStaticContent()
{
    thefly_display.fillRect(0,
                            kContentY,
                            thefly_display.width(),
                            static_cast<int16_t>(thefly_display.height() - kContentY),
                            TFT_BLACK);
    drawMainSprite();
    drawBottomCenter();
}

void ConnWaitingView::drawMainSprite()
{
    const SpriteRef sprite = main_sprite_for_mode(mode_);
    if (!sprite.data || sprite.bytes == 0 || sprite.width == 0 || sprite.height == 0)
    {
        return;
    }

    const int32_t x = (thefly_display.width() - static_cast<int32_t>(sprite.width)) / 2;
    const int32_t y = kMainSpriteCenterY - static_cast<int32_t>(sprite.height) / 2;
    SpriteDraw::drawPng(sprite.data, sprite.bytes, x, y, sprite.width, sprite.height, true);
}

void ConnWaitingView::drawBottomCenter()
{
    thefly_display.fillRect(kTextX, kBottomY, kTextWidth, kBottomSpriteSize, TFT_BLACK);

    if (mode_ == CONN_WAITING_BLUETOOTH_PAIRING)
    {
        thefly_display.setTextFont(kTextFont);
        thefly_display.setTextSize(kTextSize);
        thefly_display.setTextDatum(top_left);
        thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);
        FlyGuiTextUtil::drawWrappedText("Pairing...", kTextX, kTextY, kTextWidth, kTextMaxY, kTextLineHeight);
        return;
    }

    char text[128];
    if (mode_ == CONN_WAITING_WIFI_SCANNING)
    {
        snprintf(text, sizeof(text), "Scanning for:\n%s", targetName_);
    }
    else if (mode_ == CONN_WAITING_NTP_SYNC)
    {
        snprintf(text, sizeof(text), "Syncing time:\n%s", targetName_);
    }
    else
    {
        snprintf(text, sizeof(text), "Connecting to:\n%s", targetName_);
    }

    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextDatum(top_left);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);
    FlyGuiTextUtil::drawWrappedText(text, kTextX, kTextY, kTextWidth, kTextMaxY, kTextLineHeight);
}

void ConnWaitingView::drawHourglassFrame(uint8_t frame)
{
    const SpriteRef sprite = hourglass_sprite_for_frame(frame);
    thefly_display.fillRect(kHourglassX, kBottomY, kBottomSpriteSize, kBottomSpriteSize, TFT_BLACK);

    if (!sprite.data || sprite.bytes == 0 || sprite.width == 0 || sprite.height == 0)
    {
        return;
    }

    SpriteDraw::drawPng(sprite.data, sprite.bytes, kHourglassX, kBottomY, sprite.width, sprite.height, true);
}

bool ConnWaitingView::updateHourglass(uint32_t now, bool forced)
{
    if (!forced && now - lastHourglassDrawMs_ < kHourglassPeriodMs)
    {
        return false;
    }

    hourglassFrame_      = static_cast<uint8_t>((hourglassFrame_ + 1) % 3);
    lastHourglassDrawMs_ = now;
    drawHourglassFrame(hourglassFrame_);
    return true;
}

namespace
{

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

SpriteRef main_sprite_for_mode(ConnWaitingMode mode)
{
    switch (mode)
    {
    case CONN_WAITING_BLUETOOTH_CONNECTING:
        return {sprite_bluetooth_100,
                SPRITE_BLUETOOTH_100_WIDTH,
                SPRITE_BLUETOOTH_100_HEIGHT,
                SPRITE_BLUETOOTH_100_BYTES};
    case CONN_WAITING_BLUETOOTH_PAIRING:
        return {sprite_btpairing_100,
                SPRITE_BTPAIRING_100_WIDTH,
                SPRITE_BTPAIRING_100_HEIGHT,
                SPRITE_BTPAIRING_100_BYTES};
    case CONN_WAITING_WIFI_CONNECTING:
        return {sprite_wifi_100, SPRITE_WIFI_100_WIDTH, SPRITE_WIFI_100_HEIGHT, SPRITE_WIFI_100_BYTES};
    case CONN_WAITING_WIFI_SCANNING:
        return {sprite_wifisearch_100,
                SPRITE_WIFISEARCH_100_WIDTH,
                SPRITE_WIFISEARCH_100_HEIGHT,
                SPRITE_WIFISEARCH_100_BYTES};
    case CONN_WAITING_CLOUD:
        return {sprite_cloudupload_100,
                SPRITE_CLOUDUPLOAD_100_WIDTH,
                SPRITE_CLOUDUPLOAD_100_HEIGHT,
                SPRITE_CLOUDUPLOAD_100_BYTES};
    case CONN_WAITING_NTP_SYNC:
        return {sprite_ntpsync_100, SPRITE_NTPSYNC_100_WIDTH, SPRITE_NTPSYNC_100_HEIGHT, SPRITE_NTPSYNC_100_BYTES};
    default:
        return {};
    }
}

SpriteRef hourglass_sprite_for_frame(uint8_t frame)
{
    switch (frame % 3)
    {
    case 0:
        return {sprite_hourglass_60_1,
                SPRITE_HOURGLASS_60_1_WIDTH,
                SPRITE_HOURGLASS_60_1_HEIGHT,
                SPRITE_HOURGLASS_60_1_BYTES};
    case 1:
        return {sprite_hourglass_60_2,
                SPRITE_HOURGLASS_60_2_WIDTH,
                SPRITE_HOURGLASS_60_2_HEIGHT,
                SPRITE_HOURGLASS_60_2_BYTES};
    default:
        return {sprite_hourglass_60_3,
                SPRITE_HOURGLASS_60_3_WIDTH,
                SPRITE_HOURGLASS_60_3_HEIGHT,
                SPRITE_HOURGLASS_60_3_BYTES};
    }
}

} // namespace
