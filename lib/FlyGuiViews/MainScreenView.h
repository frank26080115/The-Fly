#pragma once

#include "../FlyGui/FlyGui.h"

class MainScreenView : public FlyGuiView
{
public:
    MainScreenView();

    void onPressLeft() override;
    void onPressMid() override;
    void onPressRight() override;

private:
    FlyGuiItem bluetoothItem_;
    FlyGuiItem wifiItem_;
    FlyGuiItem memoItem_;
    FlyGuiItem smartphoneItem_;
    FlyGuiItem laptopItem_;
    FlyGuiItem wifiHomeItem_;
};
