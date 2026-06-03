#pragma once

#include "../FlyGui/FlyGui.h"

class SplashView : public FlyGuiView
{
public:
    SplashView();

    void onLoad() override;
    void onUnload() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    bool redraw(bool forced) override;

private:
};
