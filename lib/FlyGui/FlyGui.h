#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include "Display.h"
#include <stddef.h>
#include <stdint.h>

class Button;
class FlyGui;
class FlyGuiDateTime;
class FlyGuiItem;
class FlyGuiModal;
class FlyGuiView;

using FlyGuiItemCallback = void (*)();

struct FlyGuiTouchEvent
{
    int16_t x            = 0;
    int16_t y            = 0;
    bool    pressed      = false;
    bool    justPressed  = false;
    bool    justReleased = false;
};

enum FlyGuiPollMode
{
    FLYGUI_POLL_FAST,
    FLYGUI_POLL_MEDIUM,
    FLYGUI_POLL_SLOW,
};

enum FlyGuiViewId : uint16_t
{
    FLYGUI_VIEW_SPLASH = 0,
    FLYGUI_VIEW_MAIN = 1,
    FLYGUI_VIEW_BLUETOOTH,
    FLYGUI_VIEW_RECORDING,
    FLYGUI_VIEW_WIFI,
    FLYGUI_VIEW_WEB_ACTION,
    FLYGUI_VIEW_AP_MODE,
    FLYGUI_VIEW_UPLOAD_PROGRESS,
    FLYGUI_VIEW_FILE_LIST,
    FLYGUI_VIEW_ERROR,
    FLYGUI_VIEW_CONN_WAITING,
    FLYGUI_VIEW_SCROLL,
    FLYGUI_VIEW_MODAL_DIALOG,
};

class FlyGui
{
public:
    FlyGui();
    ~FlyGui();

    static constexpr int16_t kTopBarHeight = 10;

    static constexpr int16_t topBarHeight()
    {
        return kTopBarHeight;
    }

    static void quickScreenFade();

    void        addView(FlyGuiView& view);
    bool        showView(uint16_t viewId);
    FlyGuiView* currentView() const
    {
        return currentView_;
    }

    void           setPollMode(FlyGuiPollMode mode);
    FlyGuiPollMode pollMode() const
    {
        return pollMode_;
    }
    void setAudioActive(bool active);

    void poll();
    void redraw(bool forced = false);

    void         showModal(FlyGuiModal& modal);
    void         removeModal(FlyGuiModal& modal);
    FlyGuiModal* modal() const
    {
        return modal_;
    }

    M5GFX& display()
    {
        return thefly_display;
    }

    void requestTopBarFullRedraw();

private:
    void appendView(FlyGuiView& view);
    void dispatchButtons();
    bool dispatchButtonToItem(Button& button);
    bool shouldRunScheduledPoll(FlyGuiPollMode mode, uint32_t now);
    void drawTopBar(bool forced);

    FlyGuiView*     firstView_             = nullptr;
    FlyGuiView*     lastView_              = nullptr;
    FlyGuiView*     currentView_           = nullptr;
    FlyGuiModal*    modal_                 = nullptr;
    FlyGuiPollMode  pollMode_              = FLYGUI_POLL_FAST;
    bool            topBarNeedsFullRedraw_ = false;
    uint32_t        lastScheduledPollMs_   = 0;
    uint32_t        lastTopBarDrawMs_      = 0;
    int32_t         topBarLastBattery_     = -2;
    FlyGuiDateTime* topBarDateTime_        = nullptr;
};

class FlyGuiView
{
public:
    explicit FlyGuiView(uint16_t id);
    virtual ~FlyGuiView() = default;

    uint16_t id() const
    {
        return id_;
    }
    FlyGuiView* next() const
    {
        return next_;
    }
    FlyGui* gui() const
    {
        return gui_;
    }
    FlyGuiItem* firstItem() const
    {
        return firstItem_;
    }

    void addItem(FlyGuiItem& item);
    void removeAllItems();

    bool dirty() const
    {
        return dirty_;
    }
    void setDirty(bool dirty = true)
    {
        dirty_ = dirty;
    }

    virtual void onLoad();
    virtual void onUnload();
    virtual bool handleTouch(const FlyGuiTouchEvent& event);
    virtual bool handleButtonPress(Button& button);
    virtual void redraw(bool forced);

    virtual void onPressLeft() {}
    virtual void onPressMid() {}
    virtual void onPressRight() {}

protected:
    void markClean()
    {
        dirty_ = false;
    }

private:
    friend class FlyGui;

    FlyGuiView* next_      = nullptr;
    FlyGui*     gui_       = nullptr;
    uint16_t    id_        = 0;
    FlyGuiItem* firstItem_ = nullptr;
    FlyGuiItem* lastItem_  = nullptr;
    bool        dirty_     = true;
};

class FlyGuiItem
{
public:
    FlyGuiItem(int16_t x, int16_t y, int16_t width, int16_t height, const char* mainText = nullptr, Button* button = nullptr);
    virtual ~FlyGuiItem() = default;

    FlyGuiItem* next() const
    {
        return next_;
    }
    FlyGuiView* owner() const
    {
        return owner_;
    }

    int16_t x() const
    {
        return x_;
    }
    int16_t y() const
    {
        return y_;
    }
    int16_t width() const
    {
        return width_;
    }
    int16_t height() const
    {
        return height_;
    }
    void relocate(int16_t x, int16_t y, int16_t width, int16_t height);

    const char* mainText() const
    {
        return mainText_;
    }
    void setMainText(const char* text);

    bool visible() const
    {
        return visible_;
    }
    void setVisible(bool visible);

    bool faded() const
    {
        return faded_;
    }
    void setFaded(bool faded);

    bool dirty() const
    {
        return dirty_;
    }
    void setDirty(bool dirty = true)
    {
        dirty_ = dirty;
    }

    bool contains(int16_t x, int16_t y) const;
    bool isPressed() const;

    Button* button() const
    {
        return button_;
    }
    void attachButton(Button* button)
    {
        button_ = button;
        touchable_ = button_ != nullptr || callback_ != nullptr;
    }

    bool touchable() const
    {
        return touchable_;
    }
    void setTouchable(bool touchable)
    {
        touchable_ = touchable;
    }

    void setCallback(FlyGuiItemCallback callback)
    {
        callback_ = callback;
        touchable_ = button_ != nullptr || callback_ != nullptr;
    }
    virtual bool trigger();

    void setSprite(const uint8_t* data, uint32_t width, uint32_t height, size_t byte_cnt);
    void clearSprite();

    virtual void onLoad();
    virtual void onUnload();
    virtual bool handleTouch(const FlyGuiTouchEvent& event);
    virtual bool handleButtonPress(Button& button);
    virtual void redraw(bool forced);

protected:
    void markClean()
    {
        dirty_ = false;
    }

private:
    friend class FlyGuiView;

    FlyGuiItem* next_     = nullptr;
    FlyGuiView* owner_    = nullptr;
    Button*     button_   = nullptr;
    int16_t     x_        = 0;
    int16_t     y_        = 0;
    int16_t     width_    = 0;
    int16_t     height_   = 0;
    const char* mainText_ = nullptr;
    const uint8_t* spriteData_ = nullptr;
    uint32_t    spriteWidth_   = 0;
    uint32_t    spriteHeight_  = 0;
    size_t      spriteBytes_   = 0;
    FlyGuiItemCallback callback_ = nullptr;
    bool        visible_  = true;
    bool        dirty_    = true;
    bool        pressed_  = false;
    bool        faded_    = false;
    bool        touchable_ = false;
};

// FlyGuiModal may seem redundant with ModalDialog
// FlyGuiModal is only used if we absolutely must not exit out of the current view (which will cause unloading)
// ModalDialog is used when a dialog is a part of a flow
class FlyGuiModal : public FlyGuiItem
{
public:
    FlyGuiModal(int16_t x, int16_t y, int16_t width, int16_t height, const char* mainText = nullptr);
};
