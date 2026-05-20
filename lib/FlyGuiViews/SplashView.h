#pragma once

#include "../FlyGui/FlyGui.h"

class SplashView : public FlyGuiView
{
public:
    SplashView();

    void onLoad() override;
    void redraw(bool forced) override;

private:
};
