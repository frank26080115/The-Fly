#pragma once

#include "../FlyGui/FlyGui.h"

enum ConnWaitingMode : uint8_t
{
    CONN_WAITING_BLUETOOTH_CONNECTING,
    CONN_WAITING_BLUETOOTH_PAIRING,
    CONN_WAITING_WIFI_CONNECTING,
    CONN_WAITING_WIFI_SCANNING,
    CONN_WAITING_CLOUD,
    CONN_WAITING_NTP_SYNC,
};

class ConnWaitingView : public FlyGuiView
{
public:
    ConnWaitingView(ConnWaitingMode    mode,
                    const char*        targetName,
                    FlyGuiItemCallback cancelCallback,
                    uint16_t           viewId = FLYGUI_VIEW_CONN_WAITING);

    ConnWaitingMode mode() const
    {
        return mode_;
    }
    const char* targetName() const
    {
        return targetName_;
    }
    void configure(ConnWaitingMode mode, const char* targetName);
    void setCancelCallback(FlyGuiItemCallback cancelCallback);

    void onLoad() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    bool redraw(bool forced) override;

protected:
    static constexpr size_t kTargetNameMax = 95;

    virtual void drawBottomCenter();
    virtual bool updateHourglass(uint32_t now, bool forced);

private:
    void drawStaticContent();
    void drawMainSprite();
    void drawHourglassFrame(uint8_t frame);

    ConnWaitingMode mode_                           = CONN_WAITING_BLUETOOTH_CONNECTING;
    char            targetName_[kTargetNameMax + 1] = {};
    FlyGuiItem      cancelItem_;
    uint8_t         hourglassFrame_      = 0;
    uint32_t        lastHourglassDrawMs_ = 0;
};
