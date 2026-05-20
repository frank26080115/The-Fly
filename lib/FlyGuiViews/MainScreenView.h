#pragma once

#include "../FlyGui/FlyGui.h"

class MainScreenView : public FlyGuiView
{
public:
    MainScreenView();

    void onLoad() override;
    void redraw(bool forced) override;

    void onPressLeft() override;
    void onPressMid() override;
    void onPressRight() override;

private:
    void syncBluetoothHostButtonFades();

    FlyGuiItem bluetoothItem_;
    FlyGuiItem wifiItem_;
    FlyGuiItem memoItem_;
    FlyGuiItem smartphoneItem_;
    FlyGuiItem laptopItem_;
    FlyGuiItem wifiSearchItem_;
};
