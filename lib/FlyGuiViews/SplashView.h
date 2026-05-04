#pragma once

#include "../FlyGui/FlyGui.h"

class SplashView : public FlyGuiView
{
public:
    SplashView();

    void onLoad() override;
    void redraw(M5GFX& display, bool forced) override;

private:
    bool handedOff_ = false;
};
