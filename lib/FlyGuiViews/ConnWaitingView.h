#pragma once

#include "../FlyGui/FlyGui.h"

enum ConnWaitingMode : uint8_t
{
    CONN_WAITING_BLUETOOTH_CONNECTING,
    CONN_WAITING_BLUETOOTH_PAIRING,
    CONN_WAITING_WIFI,
    CONN_WAITING_CLOUD,
    CONN_WAITING_NTP_SYNC,
};

class ConnWaitingView : public FlyGuiView
{
public:
    ConnWaitingView(ConnWaitingMode mode, const char* targetName, FlyGuiItemCallback cancelCallback);

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
    void redraw(bool forced) override;

private:
    static constexpr size_t kTargetNameMax = 95;

    void drawStaticContent();
    void drawMainSprite();
    void drawBottomCenter();
    void drawHourglassFrame(uint8_t frame);
    bool updateHourglass(uint32_t now, bool forced);

    ConnWaitingMode mode_                       = CONN_WAITING_BLUETOOTH_CONNECTING;
    char            targetName_[kTargetNameMax + 1] = {};
    FlyGuiItem      cancelItem_;
    uint8_t         hourglassFrame_             = 0;
    uint32_t        lastHourglassDrawMs_        = 0;
};
