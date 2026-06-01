#include "FlyGui.h"

#include "../Buttons/Buttons.h"
#include "../BattTracker/BattTracker.h"
#include "../Hotel/Hotel.h"
#include "../WifiManager/AsyncFsManager.h"
#include "../SpriteDraw/IconLookup.h"
#include "DiskStats.h"
#include "FlyGuiText.h"
#include "Display.h"
#include "SpriteDraw.h"
#include "sprites.h"
#include "dbg_log.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static constexpr const char* TAG = "FlyGui";
static constexpr uint32_t kSlowPollIntervalMs   = 1000 / 15;
static constexpr uint32_t kMediumPollIntervalMs = 1000 / 30;
static constexpr int16_t  kTopBarDateTimeX      = 4;
static constexpr int16_t  kTopBarDateTimeY      = 1;
static constexpr int16_t  kTopBarDateTimeWidth  = 150;
static constexpr int16_t  kTopBarDateTimeHeight = 8;
static constexpr int16_t  kTopBarBatteryWidth   = 22;

struct BatterySprite
{
    const uint8_t* data     = nullptr;
    uint32_t       width    = 0;
    uint32_t       height   = 0;
    size_t         byte_cnt = 0;
};

static int32_t batteryStatusCode();
static BatterySprite batterySpriteForStatus(int32_t status);
static const char* drawFailureStageName(SpriteDraw::DrawFailureStage stage);

FlyGui::FlyGui() : topBarDateTime_(new FlyGuiDateTime(kTopBarDateTimeX, kTopBarDateTimeY, kTopBarDateTimeWidth, kTopBarDateTimeHeight, 1.0f, 1)) {}

FlyGui::~FlyGui()
{
    delete topBarDateTime_;
}

void FlyGui::quickScreenFade()
{
    for (int16_t y = kTopBarHeight; y < thefly_display.height(); y += 3)
    {
        thefly_display.drawFastHLine(0, y, thefly_display.width(), TFT_BLACK);
        thefly_display.drawFastHLine(0, y + 1, thefly_display.width(), TFT_BLACK);
    }
}

void FlyGui::addView(FlyGuiView& view)
{
    appendView(view);
}

bool FlyGui::showView(uint16_t viewId)
{
    // Design: view selection is a simple linked-list scan by unique view identifier.
    for (FlyGuiView* view = firstView_; view; view = view->next_)
    {
        if (view->id() == viewId)
        {
            if (currentView_ == view)
            {
                return true;
            }

            if (currentView_)
            {
                currentView_->onUnload();
            }

            currentView_ = view;
            currentView_->onLoad();
            redraw(true);
            return true;
        }
    }

    return false;
}

void FlyGui::setPollMode(FlyGuiPollMode mode)
{
    pollMode_ = mode;
}

void FlyGui::setAudioActive(bool active)
{
    // Design: when audio is active, the GUI defaults to medium polling.
    setPollMode(active ? FLYGUI_POLL_MEDIUM : FLYGUI_POLL_FAST);
}

void FlyGui::poll()
{
    if (AsyncFsManager::guiShouldYield())
    {
        return;
    }

    const uint32_t       now  = millis();
    const FlyGuiPollMode mode = pollMode_;

    // Design: medium/slow polling skip touch and draw work off-schedule while audio is active.
    if (mode != FLYGUI_POLL_FAST && !shouldRunScheduledPoll(mode, now))
    {
        return;
    }

    M5.update();
    dispatchButtons();

    const auto       touch = M5.Touch.getDetail();
    FlyGuiTouchEvent event;
    event.x            = touch.x;
    event.y            = touch.y;
    event.pressed      = touch.isPressed();
    event.justPressed  = touch.wasPressed();
    event.justReleased = touch.wasReleased();

    if (event.pressed || event.justPressed || event.justReleased)
    {
        Hotel::noteUserActivity();
        if (modal_)
        {
            modal_->handleTouch(event);
        }
        else if (currentView_)
        {
            currentView_->handleTouch(event);
        }
    }

    redraw(false);
}

void FlyGui::redraw(bool forced)
{
    // Design: FlyGui owns frame-level redraw policy and invokes the active view redraw.
    drawTopBar(forced);

    if (currentView_)
    {
        currentView_->redraw(forced);
    }

    if (modal_)
    {
        modal_->redraw(forced);
    }

    if (topBarNeedsFullRedraw_)
    {
        drawTopBar(false);
    }
}

void FlyGui::showModal(FlyGuiModal& modal)
{
    // Design: modals are FlyGuiItem instances owned by FlyGui instead of FlyGuiView.
    modal_ = &modal;
    modal_->onLoad();
    modal_->setDirty();
    redraw(false);
}

void FlyGui::removeModal(FlyGuiModal& modal)
{
    if (modal_ == &modal)
    {
        modal_->onUnload();
        modal_ = nullptr;
        redraw(true);
    }
}

void FlyGui::requestTopBarFullRedraw()
{
    topBarNeedsFullRedraw_ = true;
}

void FlyGui::appendView(FlyGuiView& view)
{
    view.next_ = nullptr;
    view.gui_  = this;

    if (!firstView_)
    {
        firstView_ = &view;
        lastView_  = &view;
    }
    else
    {
        lastView_->next_ = &view;
        lastView_        = &view;
    }
}

void FlyGui::dispatchButtons()
{
    if (!currentView_)
    {
        return;
    }

    // Design: polling reads the three dedicated buttons and calls the active view handlers.
    Button* left  = buttons[TOUCHBUTTON_LEFT];
    Button* mid   = buttons[TOUCHBUTTON_CENTER];
    Button* right = buttons[TOUCHBUTTON_RIGHT];

    const bool leftPressed  = (left && left->hasPressed()) || M5.BtnA.wasPressed();
    const bool midPressed   = (mid && mid->hasPressed()) || M5.BtnB.wasPressed();
    const bool rightPressed = (right && right->hasPressed()) || M5.BtnC.wasPressed();

    if (leftPressed)
    {
        if (left)
        {
            left->clrPressed();
        }
        Hotel::noteUserActivity();
        if (!left || !dispatchButtonToItem(*left))
        {
            currentView_->onPressLeft();
        }
    }

    if (midPressed)
    {
        if (mid)
        {
            mid->clrPressed();
        }
        Hotel::noteUserActivity();
        if (!mid || !dispatchButtonToItem(*mid))
        {
            currentView_->onPressMid();
        }
    }

    if (rightPressed)
    {
        if (right)
        {
            right->clrPressed();
        }
        Hotel::noteUserActivity();
        if (!right || !dispatchButtonToItem(*right))
        {
            currentView_->onPressRight();
        }
    }
}

bool FlyGui::dispatchButtonToItem(Button& button)
{
    Hotel::noteUserActivity();

    if (modal_ && modal_->handleButtonPress(button))
    {
        return true;
    }

    return currentView_ && currentView_->handleButtonPress(button);
}

bool FlyGui::shouldRunScheduledPoll(FlyGuiPollMode mode, uint32_t now)
{
    const uint32_t intervalMs = mode == FLYGUI_POLL_SLOW ? kSlowPollIntervalMs : kMediumPollIntervalMs;
    if (now - lastScheduledPollMs_ < intervalMs)
    {
        return false;
    }

    lastScheduledPollMs_ = now;
    return true;
}

FlyGuiView::FlyGuiView(uint16_t id) : id_(id) {}

void FlyGuiView::addItem(FlyGuiItem& item)
{
    // Design: FlyGuiView owns FlyGuiItems as a linked list.
    item.next_  = nullptr;
    item.owner_ = this;
    if (!firstItem_)
    {
        firstItem_ = &item;
        lastItem_  = &item;
    }
    else
    {
        lastItem_->next_ = &item;
        lastItem_        = &item;
    }
    setDirty();
}

void FlyGuiView::removeAllItems()
{
    FlyGuiItem* item = firstItem_;
    while (item)
    {
        FlyGuiItem* next = item->next_;
        item->owner_     = nullptr;
        item->next_      = nullptr;
        item             = next;
    }
    firstItem_ = nullptr;
    lastItem_  = nullptr;
    setDirty();
}

void FlyGuiView::onLoad()
{
    for (FlyGuiItem* item = firstItem_; item; item = item->next_)
    {
        item->onLoad();
    }
    setDirty();
}

void FlyGuiView::onUnload()
{
    for (FlyGuiItem* item = firstItem_; item; item = item->next_)
    {
        item->onUnload();
    }

    thefly_display.fillRect(0, FlyGui::kTopBarHeight, thefly_display.width(), thefly_display.height() - FlyGui::kTopBarHeight, TFT_BLACK);
    //FlyGui::requestTopBarFullRedraw(); // optional, keep this here as a reminder
}

bool FlyGuiView::handleTouch(const FlyGuiTouchEvent& event)
{
    // Design: touch propagates through the active view's items until one handles it.
    for (FlyGuiItem* item = firstItem_; item; item = item->next_)
    {
        if (item->handleTouch(event))
        {
            return true;
        }
    }
    return false;
}

bool FlyGuiView::handleButtonPress(Button& button)
{
    for (FlyGuiItem* item = firstItem_; item; item = item->next_)
    {
        if (item->handleButtonPress(button))
        {
            return true;
        }
    }
    return false;
}

void FlyGuiView::redraw(bool forced)
{
    // Design: views and items both have dirty-aware redraw functions.
    if (!forced && !dirty_)
    {
        for (FlyGuiItem* item = firstItem_; item; item = item->next_)
        {
            if (item->dirty())
            {
                item->redraw(false);
            }
        }
        return;
    }

    for (FlyGuiItem* item = firstItem_; item; item = item->next_)
    {
        item->redraw(forced);
    }
    markClean();
}

FlyGuiItem::FlyGuiItem(int16_t x, int16_t y, int16_t width, int16_t height, const char* mainText, Button* button) : button_(button), x_(x), y_(y), width_(width), height_(height), mainText_(mainText), touchable_(button != nullptr) {}

void FlyGuiItem::relocate(int16_t x, int16_t y, int16_t width, int16_t height)
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
    if (owner_)
    {
        owner_->setDirty();
    }
}

void FlyGuiItem::setMainText(const char* text)
{
    if (mainText_ == text || (mainText_ && text && strcmp(mainText_, text) == 0))
    {
        return;
    }
    mainText_ = text;
    setDirty();
}

void FlyGuiItem::onLoad()
{
    setDirty();
}

void FlyGuiItem::onUnload()
{
}

void FlyGuiItem::setVisible(bool visible)
{
    // Design: public visibility changes set the item's dirty flag appropriately.
    if (visible_ != visible)
    {
        visible_ = visible;
        setDirty();
        if (owner_)
        {
            owner_->setDirty();
        }
    }
}

void FlyGuiItem::setFaded(bool faded)
{
    if (faded_ == faded)
    {
        return;
    }

    faded_ = faded;
    setDirty();
}

bool FlyGuiItem::contains(int16_t x, int16_t y) const
{
    return visible_ && x >= x_ && y >= y_ && x < x_ + width_ && y < y_ + height_;
}

bool FlyGuiItem::isPressed() const
{
    return pressed_ || (button_ && const_cast<Button*>(button_)->isPressed());
}

bool FlyGuiItem::trigger(uint32_t pressDurationMs)
{
    if (!visible_ || !touchable_)
    {
        return false;
    }

    lastPressDurationMs_ = pressDurationMs;

    if (callback_)
    {
        callback_(pressDurationMs);
    }

    return true;
}

void FlyGuiItem::setSprite(const uint8_t* data, uint32_t width, uint32_t height, size_t byte_cnt)
{
    spriteData_   = data;
    spriteWidth_  = width;
    spriteHeight_ = height;
    spriteBytes_  = byte_cnt;
    overlayData_ = nullptr;
    overlayWidth_ = 0;
    overlayHeight_ = 0;
    overlayBytes_ = 0;
    overlayOffsetX_ = 0;
    overlayOffsetY_ = 0;
    setDirty();
}

void FlyGuiItem::setSprite(const sprite_desc_t& sprite)
{
    spriteData_   = sprite.data;
    spriteWidth_  = sprite.width;
    spriteHeight_ = sprite.height;
    spriteBytes_  = sprite.byte_cnt;
    overlayData_ = sprite.overlay_data;
    overlayWidth_ = sprite.overlay_width;
    overlayHeight_ = sprite.overlay_height;
    overlayBytes_ = sprite.overlay_byte_cnt;
    overlayOffsetX_ = static_cast<int16_t>(sprite.overlay_offset_x);
    overlayOffsetY_ = static_cast<int16_t>(sprite.overlay_offset_y);
    setDirty();
}

void FlyGuiItem::clearSprite()
{
    spriteData_   = nullptr;
    spriteWidth_  = 0;
    spriteHeight_ = 0;
    spriteBytes_  = 0;
    overlayData_ = nullptr;
    overlayWidth_ = 0;
    overlayHeight_ = 0;
    overlayBytes_ = 0;
    overlayOffsetX_ = 0;
    overlayOffsetY_ = 0;
    setDirty();
}

bool FlyGuiItem::handleTouch(const FlyGuiTouchEvent& event)
{
    if (!visible_ || !touchable_)
    {
        return false;
    }

    const bool hit = contains(event.x, event.y);
    const bool wasPressed = pressed_;
    const uint32_t now = millis();

    if (hit && event.justPressed)
    {
        pressStartedMs_ = now;
        lastPressDurationMs_ = 0;
    }

    if (event.justReleased || !event.pressed)
    {
        pressed_ = false;
    }

    if (!hit)
    {
        if (wasPressed)
        {
            pressed_ = false;
            setDirty();
            if (owner_)
            {
                owner_->setDirty();
            }
        }
        return false;
    }

    if (event.justReleased && wasPressed)
    {
        const uint32_t pressDurationMs = now - pressStartedMs_;
        setDirty();
        if (owner_)
        {
            owner_->setDirty();
        }
        return trigger(pressDurationMs);
    }

    if (event.pressed || event.justPressed)
    {
        if (!wasPressed)
        {
            pressStartedMs_ = now;
            lastPressDurationMs_ = 0;
        }
        pressed_ = true;
        if (width_ > 0 && height_ > 0)
        {
            thefly_display.drawRect(x_, y_, width_, height_, TFT_WHITE);
        }
        if (!wasPressed)
        {
            setDirty();
            if (owner_)
            {
                owner_->setDirty();
            }
        }
    }

    return true;
}

bool FlyGuiItem::handleButtonPress(Button& button)
{
    if (&button != button_ || !visible_ || !touchable_)
    {
        return false;
    }

    FlyGuiTouchEvent event;
    event.x           = x_ + width_ / 2;
    event.y           = y_ + height_ / 2;
    event.pressed     = true;
    event.justPressed = true;

    const bool handled = handleTouch(event);
    pressed_           = button.isPressed();
    trigger(0);
    return handled;
}

void FlyGuiItem::redraw(bool forced)
{
    // Design: redraw accepts a forced flag and otherwise honors the item dirty flag.
    if (!visible_ || (!forced && !dirty_))
    {
        return;
    }

    if (spriteData_ && spriteBytes_ > 0 && spriteWidth_ > 0 && spriteHeight_ > 0)
    {
        const uint8_t brightness = faded_ ? (SpriteDraw::PNG_BRTNESS_25 | SpriteDraw::PNG_DITHER_FLAG) : SpriteDraw::PNG_BRTNESS_100;
        const SpriteDraw::DrawResult result = SpriteDraw::drawPng(spriteData_, spriteBytes_, x_, y_, spriteWidth_, spriteHeight_, true, brightness);

        if (!result.ok)
        {
            return;
        }

        if (overlayData_ && overlayBytes_ > 0 && overlayWidth_ > 0 && overlayHeight_ > 0)
        {
            SpriteDraw::drawPng(overlayData_,
                                overlayBytes_,
                                x_ + overlayOffsetX_,
                                y_ + overlayOffsetY_,
                                overlayWidth_,
                                overlayHeight_,
                                true,
                                brightness);
        }
    }
    else if (width_ > 0 && height_ > 0)
    {
        thefly_display.fillRect(x_, y_, width_, height_, TFT_BLACK);
    }

    if (mainText_)
    {
        thefly_display.drawString(mainText_, x_, y_);
    }

    markClean();
}

FlyGuiModal::FlyGuiModal(int16_t x, int16_t y, int16_t width, int16_t height, const char* mainText) : FlyGuiItem(x, y, width, height, mainText) {}

static const char* drawFailureStageName(SpriteDraw::DrawFailureStage stage)
{
    switch (stage)
    {
    case SpriteDraw::DRAW_FAILURE_NONE:
        return "none";
    case SpriteDraw::DRAW_FAILURE_INVALID_ARGUMENT:
        return "invalid_arg";
    case SpriteDraw::DRAW_FAILURE_ALLOC:
        return "alloc";
    case SpriteDraw::DRAW_FAILURE_PREPARE:
        return "prepare";
    case SpriteDraw::DRAW_FAILURE_SIZE_MISMATCH:
        return "size_mismatch";
    case SpriteDraw::DRAW_FAILURE_DECODE:
        return "decode";
    default:
        return "unknown";
    }
}

void FlyGui::drawTopBar(bool forced)
{
    const int32_t  battery    = batteryStatusCode();
    const uint32_t currentMs  = millis();
    const bool     fullRedraw = forced || topBarNeedsFullRedraw_;

    if (!fullRedraw && battery == topBarLastBattery_ && currentMs - lastTopBarDrawMs_ < 1000)
    {
        return;
    }

    // Design: FlyGui is responsible for drawing the top bar with date/time and battery status.
    if (fullRedraw)
    {
        thefly_display.fillRect(0, 0, thefly_display.width(), FlyGui::kTopBarHeight, TFT_BLACK);
    }

    if (topBarDateTime_)
    {
        // Design: top-bar date/time drawing reuses the FlyGuiDateTime text item.
        topBarDateTime_->redraw(fullRedraw);
    }

    if (fullRedraw || battery != topBarLastBattery_)
    {
        const int32_t batteryX = thefly_display.width() - kTopBarBatteryWidth;
        thefly_display.fillRect(batteryX, 0, kTopBarBatteryWidth, FlyGui::kTopBarHeight, TFT_BLACK);

        const BatterySprite sprite = batterySpriteForStatus(battery);
        if (sprite.data && sprite.byte_cnt > 0 && sprite.width > 0 && sprite.height > 0)
        {
            const int32_t x = thefly_display.width() - 4 - static_cast<int32_t>(sprite.width);
            const int32_t y = (FlyGui::kTopBarHeight - static_cast<int32_t>(sprite.height)) / 2;
            SpriteDraw::drawPng(sprite.data, sprite.byte_cnt, x, y, sprite.width, sprite.height, true);
        }
    }

    DiskStats::drawDiskSpaceWarning();

    topBarLastBattery_     = battery;
    lastTopBarDrawMs_      = currentMs;
    topBarNeedsFullRedraw_ = false;
}

static int32_t batteryStatusCode()
{
    int32_t level = -1;
    switch (BattTracker::level())
    {
    case BattTracker::ChargeLevel::low:
        level = 0;
        break;
    case BattTracker::ChargeLevel::medium:
        level = 1;
        break;
    case BattTracker::ChargeLevel::high:
        level = 2;
        break;
    case BattTracker::ChargeLevel::unknown:
    default:
        return -1;
    }

    return level | (BattTracker::isCharging() ? 0x04 : 0x00);
}

static BatterySprite batterySpriteForStatus(int32_t status)
{
    switch (status)
    {
    case 0x00:
        return { sprite_batt_low, SPRITE_BATT_LOW_WIDTH, SPRITE_BATT_LOW_HEIGHT, SPRITE_BATT_LOW_BYTES };
    case 0x01:
        return { sprite_batt_medium, SPRITE_BATT_MEDIUM_WIDTH, SPRITE_BATT_MEDIUM_HEIGHT, SPRITE_BATT_MEDIUM_BYTES };
    case 0x02:
        return { sprite_batt_full, SPRITE_BATT_FULL_WIDTH, SPRITE_BATT_FULL_HEIGHT, SPRITE_BATT_FULL_BYTES };
    case 0x04:
        return { sprite_batt_low_charging, SPRITE_BATT_LOW_CHARGING_WIDTH, SPRITE_BATT_LOW_CHARGING_HEIGHT, SPRITE_BATT_LOW_CHARGING_BYTES };
    case 0x05:
        return { sprite_batt_medium_charging, SPRITE_BATT_MEDIUM_CHARGING_WIDTH, SPRITE_BATT_MEDIUM_CHARGING_HEIGHT, SPRITE_BATT_MEDIUM_CHARGING_BYTES };
    case 0x06:
        return { sprite_batt_full_charging, SPRITE_BATT_FULL_CHARGING_WIDTH, SPRITE_BATT_FULL_CHARGING_HEIGHT, SPRITE_BATT_FULL_CHARGING_BYTES };
    default:
        return {};
    }
}
