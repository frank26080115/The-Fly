#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <stdint.h>

class FlyGui;
class FlyGuiView;
class FlyGuiDateTime;

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
};

class FlyGuiItem
{
public:
    FlyGuiItem(int16_t x, int16_t y, int16_t width, int16_t height, const char* imagePath = nullptr, const char* mainText = nullptr);
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

    const char* imagePath() const
    {
        return imagePath_;
    }
    const char* mainText() const
    {
        return mainText_;
    }
    void setMainText(const char* text);

    void setImageBuffer(void* buffer)
    {
        imageBuffer_ = buffer;
    }
    void* imageBuffer() const
    {
        return imageBuffer_;
    }
    size_t imageBufferSize() const
    {
        return imageBufferSize_;
    }
    void* findSiblingImageBuffer() const;

    bool visible() const
    {
        return visible_;
    }
    void setVisible(bool visible);

    bool dirty() const
    {
        return dirty_;
    }
    void setDirty(bool dirty = true)
    {
        dirty_ = dirty;
    }

    bool contains(int16_t x, int16_t y) const;

    virtual void onLoad();
    virtual void onUnload();
    virtual bool handleTouch(const FlyGuiTouchEvent& event);
    virtual void redraw(M5GFX& display, bool forced);

protected:
    void markClean()
    {
        dirty_ = false;
    }

private:
    friend class FlyGuiView;

    FlyGuiItem* next_            = nullptr;
    FlyGuiView* owner_           = nullptr;
    int16_t     x_               = 0;
    int16_t     y_               = 0;
    int16_t     width_           = 0;
    int16_t     height_          = 0;
    const char* imagePath_       = nullptr;
    const char* mainText_        = nullptr;
    void*       imageBuffer_     = nullptr;
    size_t      imageBufferSize_ = 0;
    bool        ownsImageBuffer_ = false;
    bool        visible_         = true;
    bool        dirty_           = true;
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

    void  addItem(FlyGuiItem& item);
    void  removeAllItems();
    void* findLoadedImageBuffer(const FlyGuiItem& requester, const char* imagePath) const;

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
    virtual void redraw(M5GFX& display, bool forced);

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

class FlyGuiModal : public FlyGuiItem
{
public:
    FlyGuiModal(int16_t x, int16_t y, int16_t width, int16_t height, const char* imagePath = nullptr, const char* mainText = nullptr);
};

class FlyGui
{
public:
    explicit FlyGui(M5GFX& display = M5.Display);
    ~FlyGui();

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
        return display_;
    }

private:
    void drawTopBar(bool forced);
    void dispatchButtons();
    bool shouldRunScheduledPoll(FlyGuiPollMode mode, uint32_t now);
    void appendView(FlyGuiView& view);

    M5GFX&          display_;
    FlyGuiView*     firstView_           = nullptr;
    FlyGuiView*     lastView_            = nullptr;
    FlyGuiView*     currentView_         = nullptr;
    FlyGuiModal*    modal_               = nullptr;
    FlyGuiPollMode  pollMode_            = FLYGUI_POLL_FAST;
    uint32_t        lastScheduledPollMs_ = 0;
    uint32_t        lastTopBarDrawMs_    = 0;
    int32_t         topBarLastBattery_   = -2;
    FlyGuiDateTime* topBarDateTime_      = nullptr;
};
