#pragma once

#include "../FlyGui/FlyGui.h"

class WifiApModeView : public FlyGuiView
{
public:
    WifiApModeView();

    void onLoad() override;
    void onUnload() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    void redraw(bool forced) override;
    void onPressRight() override;

private:
    void toggleSensitive();
    void syncEyeSprite();
    void resetQrHold();
    void drawQrHoldFade();
    void showQrCode();
    void drawStaticContent();
    void drawCredentials();
    void drawClientInfo(bool forced);
    void drawStatsLine(bool forced);
    void formatStatsLine(char* out, size_t out_size) const;
    bool makeWifiQrText(char* out, size_t out_size) const;

    FlyGuiItem wifiIcon_;
    FlyGuiItem securityIcon_;
    FlyGuiItem eyeItem_;
    bool       showSensitive_    = false;
    uint8_t    statsIndex_       = 0;
    uint32_t   lastClientDrawMs_ = 0;
    uint32_t   lastStatsDrawMs_  = 0;
    uint32_t   qrHoldStartedMs_  = 0;
    bool       qrHoldActive_     = false;
    bool       qrHoldFadeDrawn_  = false;

    static WifiApModeView* activeView_;
};
