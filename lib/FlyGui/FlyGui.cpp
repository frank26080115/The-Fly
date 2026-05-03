#include "FlyGui.h"

#include "../Buttons/Buttons.h"
#include "FlyGuiText.h"
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <stdlib.h>
#include <string.h>

static constexpr uint32_t kSlowPollIntervalMs   = 1000 / 15;
static constexpr uint32_t kMediumPollIntervalMs = 1000 / 30;
static constexpr uint16_t kTopBarHeight         = 22;
static constexpr int16_t  kTopBarDateTimeX      = 4;
static constexpr int16_t  kTopBarDateTimeY      = 4;
static constexpr int16_t  kTopBarDateTimeWidth  = 150;
static constexpr int16_t  kTopBarDateTimeHeight = 14;
static constexpr int16_t  kTopBarBatteryWidth   = 44;

static bool openFlyGuiImageFile(const char* path, fs::File& file)
{
    file = LittleFS.open(path, "r");
    if (file)
    {
        return true;
    }

    file = SD.open(path, "r");
    return file;
}

static bool loadFlyGuiImageFile(const char* path, void** buffer, size_t* bufferSize)
{
    if (!path || !*path || !buffer || !bufferSize)
    {
        return false;
    }

    fs::File file;
    if (!openFlyGuiImageFile(path, file))
    {
        return false;
    }

    const size_t size = file.size();
    if (size == 0)
    {
        file.close();
        return false;
    }

    void* data = malloc(size);
    if (!data)
    {
        file.close();
        return false;
    }

    const size_t bytesRead = file.read(static_cast<uint8_t*>(data), size);
    file.close();

    if (bytesRead != size)
    {
        free(data);
        return false;
    }

    *buffer     = data;
    *bufferSize = size;
    return true;
}

FlyGuiItem::FlyGuiItem(int16_t x, int16_t y, int16_t width, int16_t height, const char* imagePath, const char* mainText) : x_(x), y_(y), width_(width), height_(height), imagePath_(imagePath), mainText_(mainText) {}

void FlyGuiItem::setMainText(const char* text)
{
    if (mainText_ == text || (mainText_ && text && strcmp(mainText_, text) == 0))
    {
        return;
    }
    mainText_ = text;
    setDirty();
}

void* FlyGuiItem::findSiblingImageBuffer() const
{
    return owner_ ? owner_->findLoadedImageBuffer(*this, imagePath_) : nullptr;
}

void FlyGuiItem::onLoad()
{
    // Design: item load can prepare a RAM-backed icon buffer from its image path.
    if (imageBuffer_ || !imagePath_ || !*imagePath_)
    {
        return;
    }

    if (owner_)
    {
        for (FlyGuiItem* sibling = owner_->firstItem(); sibling; sibling = sibling->next())
        {
            if (sibling != this && sibling->imageBuffer() && sibling->imagePath() && strcmp(sibling->imagePath(), imagePath_) == 0)
            {
                // Design: duplicate image paths borrow an already-loaded sibling buffer.
                imageBuffer_     = sibling->imageBuffer();
                imageBufferSize_ = sibling->imageBufferSize();
                ownsImageBuffer_ = false;
                setDirty();
                return;
            }
        }
    }

    // Design: when no sibling has the image, malloc a buffer and remember that we own it.
    if (loadFlyGuiImageFile(imagePath_, &imageBuffer_, &imageBufferSize_))
    {
        ownsImageBuffer_ = true;
        setDirty();
    }
}

void FlyGuiItem::onUnload()
{
    // Design: only the item that malloc'd the image buffer frees it.
    if (ownsImageBuffer_ && imageBuffer_)
    {
        free(imageBuffer_);
    }

    imageBuffer_     = nullptr;
    imageBufferSize_ = 0;
    ownsImageBuffer_ = false;
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

bool FlyGuiItem::contains(int16_t x, int16_t y) const
{
    return visible_ && x >= x_ && y >= y_ && x < x_ + width_ && y < y_ + height_;
}

bool FlyGuiItem::handleTouch(const FlyGuiTouchEvent& event)
{
    return contains(event.x, event.y);
}

void FlyGuiItem::redraw(M5GFX& display, bool forced)
{
    // Design: redraw accepts a forced flag and otherwise honors the item dirty flag.
    if (!visible_ || (!forced && !dirty_))
    {
        return;
    }

    if (imageBuffer_ && imageBufferSize_ > 0)
    {
        // Design: loaded PNG bytes are drawn through M5GFX's PNG decoder.
        lgfx::PointerWrapper png(static_cast<const uint8_t*>(imageBuffer_), imageBufferSize_);
        display.drawPng(&png, x_, y_, width_, height_);
    }

    if (mainText_)
    {
        display.drawString(mainText_, x_, y_);
    }
    markClean();
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

void* FlyGuiView::findLoadedImageBuffer(const FlyGuiItem& requester, const char* imagePath) const
{
    // Design: items can ask their owning view to find a sibling that already loaded the same image.
    if (!imagePath || !*imagePath)
    {
        return nullptr;
    }

    for (FlyGuiItem* item = firstItem_; item; item = item->next_)
    {
        if (item != &requester && item->imageBuffer() && item->imagePath() && strcmp(item->imagePath(), imagePath) == 0)
        {
            return item->imageBuffer();
        }
    }

    return nullptr;
}

void FlyGuiView::onLoad()
{
    // Design: view load propagates to its items for RAM-backed image/icon setup.
    for (FlyGuiItem* item = firstItem_; item; item = item->next_)
    {
        item->onLoad();
    }
    setDirty();
}

void FlyGuiView::onUnload()
{
    // Design: view unload propagates to its items so derived items can release RAM buffers.
    for (FlyGuiItem* item = firstItem_; item; item = item->next_)
    {
        item->onUnload();
    }
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

void FlyGuiView::redraw(M5GFX& display, bool forced)
{
    // Design: views and items both have dirty-aware redraw functions.
    if (!forced && !dirty_)
    {
        for (FlyGuiItem* item = firstItem_; item; item = item->next_)
        {
            if (item->dirty())
            {
                item->redraw(display, false);
            }
        }
        return;
    }

    for (FlyGuiItem* item = firstItem_; item; item = item->next_)
    {
        item->redraw(display, forced);
    }
    markClean();
}

FlyGuiModal::FlyGuiModal(int16_t x, int16_t y, int16_t width, int16_t height, const char* imagePath, const char* mainText) : FlyGuiItem(x, y, width, height, imagePath, mainText) {}

FlyGui::FlyGui(M5GFX& display) : display_(display), topBarDateTime_(new FlyGuiDateTime(kTopBarDateTimeX, kTopBarDateTimeY, kTopBarDateTimeWidth, kTopBarDateTimeHeight, 1.0f, 1)) {}

FlyGui::~FlyGui()
{
    delete topBarDateTime_;
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
        currentView_->redraw(display_, forced);
    }

    if (modal_)
    {
        modal_->redraw(display_, forced);
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

void FlyGui::drawTopBar(bool forced)
{
    const int32_t  battery   = M5.Power.getBatteryLevel();
    const uint32_t currentMs = millis();

    if (!forced && battery == topBarLastBattery_ && currentMs - lastTopBarDrawMs_ < 1000)
    {
        return;
    }

    // Design: FlyGui is responsible for drawing the top bar with date/time and battery status.
    if (forced)
    {
        display_.fillRect(0, 0, display_.width(), kTopBarHeight, TFT_BLACK);
    }

    if (topBarDateTime_)
    {
        // Design: top-bar date/time drawing reuses the FlyGuiDateTime text item.
        topBarDateTime_->redraw(display_, forced);
    }

    if (forced || battery != topBarLastBattery_)
    {
        const int32_t batteryX = display_.width() - kTopBarBatteryWidth;
        display_.fillRect(batteryX, 0, kTopBarBatteryWidth, kTopBarHeight, TFT_BLACK);

        if (battery >= 0)
        {
            char batteryText[8];
            snprintf(batteryText, sizeof(batteryText), "%ld%%", static_cast<long>(battery));
            display_.setTextColor(TFT_WHITE, TFT_BLACK);
            display_.setTextSize(1);
            display_.setTextDatum(top_right);
            display_.drawString(batteryText, display_.width() - 4, 4);
            display_.setTextDatum(top_left);
        }
    }

    topBarLastBattery_ = battery;
    lastTopBarDrawMs_  = currentMs;
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

    if (left && left->hasPressed())
    {
        left->clrPressed();
        currentView_->onPressLeft();
    }

    if (mid && mid->hasPressed())
    {
        mid->clrPressed();
        currentView_->onPressMid();
    }

    if (right && right->hasPressed())
    {
        right->clrPressed();
        currentView_->onPressRight();
    }
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
