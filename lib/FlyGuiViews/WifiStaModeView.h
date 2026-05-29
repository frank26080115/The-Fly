#pragma once

#include "../FlyGui/FlyGui.h"

class WifiStaModeView : public FlyGuiView
{
public:
    WifiStaModeView();

    void configure(bool showDismissButton);

    void onLoad() override;
    void onUnload() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    void redraw(bool forced) override;
    void onPressRight() override;

private:
    static void onDismissTriggered(uint32_t pressDurationMs);

    void dismiss();
    void drawStaticContent();
    void drawConnectionInfo();
    void drawNetworkDetails();
    void drawStopHint();
    void drawStatsLine(bool forced);
    void formatStatsLine(char* out, size_t out_size) const;

    FlyGuiItem wifiIcon_;
    FlyGuiItem dismissItem_;
    bool       showDismissButton_ = false;
    uint8_t    statsIndex_ = 0;
    uint32_t   lastStatsDrawMs_ = 0;

    static WifiStaModeView* activeView_;
};
