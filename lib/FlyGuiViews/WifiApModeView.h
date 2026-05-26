#pragma once

#include "../FlyGui/FlyGui.h"

class WifiApModeView : public FlyGuiView
{
public:
    WifiApModeView();

    void onLoad() override;
    void onUnload() override;
    void redraw(bool forced) override;
    void onPressRight() override;

private:
    static void onEyeTriggered(uint32_t pressDurationMs);

    void toggleSensitive();
    void syncEyeSprite();
    void drawStaticContent();
    void drawCredentials();
    void drawClientInfo(bool forced);
    void drawStatsLine(bool forced);
    void formatStatsLine(char* out, size_t out_size) const;

    FlyGuiItem wifiIcon_;
    FlyGuiItem securityIcon_;
    FlyGuiItem eyeItem_;
    bool       showSensitive_ = false;
    uint8_t    statsIndex_ = 0;
    uint32_t   lastClientDrawMs_ = 0;
    uint32_t   lastStatsDrawMs_ = 0;

    static WifiApModeView* activeView_;
};
