#include "MainScreenView.h"

#include "BtHostList.h"
#include "sprites.h"

extern BtHostList* bt_host_list;

extern void onclick_main_bluetooth();
extern void onclick_main_info();
extern void onclick_main_wifi();
extern void onclick_main_memo();
extern void onclick_main_smartphone();
extern void onclick_main_laptop();

namespace
{
constexpr int16_t kButtonWidth  = 106;
constexpr int16_t kButtonHeight = 114;
constexpr int16_t kGridX        = 1;
constexpr int16_t kGridY        = FlyGui::topBarHeight();
constexpr int16_t kCol0         = kGridX;
constexpr int16_t kCol1         = kGridX + kButtonWidth;
constexpr int16_t kCol2         = kGridX + kButtonWidth * 2;
constexpr int16_t kRow0         = kGridY;
constexpr int16_t kRow1         = kGridY + kButtonHeight;
} // namespace

MainScreenView::MainScreenView()
    : // Design: main screen lets the user choose bluetooth, info, memo, or files.
      FlyGuiView(FLYGUI_VIEW_MAIN),
      bluetoothItem_(kCol0, kRow0, kButtonWidth, kButtonHeight),
      infoItem_(kCol1, kRow0, kButtonWidth, kButtonHeight),
      memoItem_(kCol2, kRow0, kButtonWidth, kButtonHeight),
      smartphoneItem_(kCol0, kRow1, kButtonWidth, kButtonHeight),
      laptopItem_(kCol1, kRow1, kButtonWidth, kButtonHeight),
      wifiItem_(kCol2, kRow1, kButtonWidth, kButtonHeight)
{
    bluetoothItem_.setSprite(sprit_btn_bluetooth, SPRIT_BTN_BLUETOOTH_WIDTH, SPRIT_BTN_BLUETOOTH_HEIGHT, SPRIT_BTN_BLUETOOTH_BYTES);
    bluetoothItem_.setCallback(onclick_main_bluetooth);
    addItem(bluetoothItem_);

    infoItem_.setSprite(sprit_btn_info, SPRIT_BTN_INFO_WIDTH, SPRIT_BTN_INFO_HEIGHT, SPRIT_BTN_INFO_BYTES);
    infoItem_.setCallback(onclick_main_info);
    addItem(infoItem_);

    memoItem_.setSprite(sprit_btn_memo, SPRIT_BTN_MEMO_WIDTH, SPRIT_BTN_MEMO_HEIGHT, SPRIT_BTN_MEMO_BYTES);
    memoItem_.setCallback(onclick_main_memo);
    addItem(memoItem_);

    smartphoneItem_.setSprite(sprit_btn_smartphone, SPRIT_BTN_SMARTPHONE_WIDTH, SPRIT_BTN_SMARTPHONE_HEIGHT, SPRIT_BTN_SMARTPHONE_BYTES);
    smartphoneItem_.setCallback(onclick_main_smartphone);
    addItem(smartphoneItem_);

    laptopItem_.setSprite(sprit_btn_laptop, SPRIT_BTN_LAPTOP_WIDTH, SPRIT_BTN_LAPTOP_HEIGHT, SPRIT_BTN_LAPTOP_BYTES);
    laptopItem_.setCallback(onclick_main_laptop);
    addItem(laptopItem_);

    wifiItem_.setSprite(sprit_btn_wifi, SPRIT_BTN_WIFI_WIDTH, SPRIT_BTN_WIFI_HEIGHT, SPRIT_BTN_WIFI_BYTES);
    wifiItem_.setCallback(onclick_main_wifi);
    addItem(wifiItem_);
}

void MainScreenView::onLoad()
{
    syncBluetoothHostButtonFades();
    FlyGuiView::onLoad();
}

void MainScreenView::redraw(bool forced)
{
    syncBluetoothHostButtonFades();
    FlyGuiView::redraw(forced);
}

void MainScreenView::onPressLeft()
{
    smartphoneItem_.trigger();
}

void MainScreenView::onPressMid()
{
    laptopItem_.trigger();
}

void MainScreenView::onPressRight()
{
    wifiItem_.trigger();
}

void MainScreenView::syncBluetoothHostButtonFades()
{
    const bool phoneMissing  = !bt_host_list || bt_host_list->getFirstPhone() == nullptr;
    const bool laptopMissing = !bt_host_list || bt_host_list->getFirstLaptop() == nullptr;

    if (smartphoneItem_.faded() != phoneMissing)
    {
        smartphoneItem_.setFaded(phoneMissing);
        setDirty();
    }

    if (laptopItem_.faded() != laptopMissing)
    {
        laptopItem_.setFaded(laptopMissing);
        setDirty();
    }
}
