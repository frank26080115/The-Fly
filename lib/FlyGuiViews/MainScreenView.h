#pragma once

#include "../FlyGui/FlyGui.h"

class MainScreenView : public FlyGuiView
{
public:
    MainScreenView();

    void onLoad() override;
    bool redraw(bool forced) override;

    void onPressLeft() override;
    void onPressMid() override;
    void onPressRight() override;

    void showMemoStartingFeedback();

private:
    void syncFilesIcon();
    void syncBluetoothHostButtonFades();

    FlyGuiItem bluetoothItem_;
    FlyGuiItem filesItem_;
    FlyGuiItem memoItem_;
    FlyGuiItem smartphoneItem_;
    FlyGuiItem laptopItem_;
    FlyGuiItem wifiItem_;
};
