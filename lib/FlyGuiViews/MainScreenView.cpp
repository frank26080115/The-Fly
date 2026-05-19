#include "MainScreenView.h"

#include "sprites.h"

extern void main_screen_bluetooth();
extern void main_screen_wifi();
extern void main_screen_memo();
extern void main_screen_smartphone();
extern void main_screen_laptop();
extern void main_screen_wifihome();

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
    : // Design: main screen lets the user choose bluetooth, wifi, memo, or files.
      FlyGuiView(FLYGUI_VIEW_MAIN),
      bluetoothItem_(kCol0, kRow0, kButtonWidth, kButtonHeight),
      wifiItem_(kCol1, kRow0, kButtonWidth, kButtonHeight),
      memoItem_(kCol2, kRow0, kButtonWidth, kButtonHeight),
      smartphoneItem_(kCol0, kRow1, kButtonWidth, kButtonHeight),
      laptopItem_(kCol1, kRow1, kButtonWidth, kButtonHeight),
      wifiHomeItem_(kCol2, kRow1, kButtonWidth, kButtonHeight)
{
    bluetoothItem_.setSprite(sprit_btn_bluetooth, SPRIT_BTN_BLUETOOTH_WIDTH, SPRIT_BTN_BLUETOOTH_HEIGHT, SPRIT_BTN_BLUETOOTH_BYTES);
    bluetoothItem_.setCallback(main_screen_bluetooth);
    addItem(bluetoothItem_);

    wifiItem_.setSprite(sprit_btn_wifi, SPRIT_BTN_WIFI_WIDTH, SPRIT_BTN_WIFI_HEIGHT, SPRIT_BTN_WIFI_BYTES);
    wifiItem_.setCallback(main_screen_wifi);
    addItem(wifiItem_);

    memoItem_.setSprite(sprit_btn_memo, SPRIT_BTN_MEMO_WIDTH, SPRIT_BTN_MEMO_HEIGHT, SPRIT_BTN_MEMO_BYTES);
    memoItem_.setCallback(main_screen_memo);
    addItem(memoItem_);

    smartphoneItem_.setSprite(sprit_btn_smartphone, SPRIT_BTN_SMARTPHONE_WIDTH, SPRIT_BTN_SMARTPHONE_HEIGHT, SPRIT_BTN_SMARTPHONE_BYTES);
    smartphoneItem_.setCallback(main_screen_smartphone);
    addItem(smartphoneItem_);

    laptopItem_.setSprite(sprit_btn_laptop, SPRIT_BTN_LAPTOP_WIDTH, SPRIT_BTN_LAPTOP_HEIGHT, SPRIT_BTN_LAPTOP_BYTES);
    laptopItem_.setCallback(main_screen_laptop);
    addItem(laptopItem_);

    wifiHomeItem_.setSprite(sprit_btn_wifihome, SPRIT_BTN_WIFIHOME_WIDTH, SPRIT_BTN_WIFIHOME_HEIGHT, SPRIT_BTN_WIFIHOME_BYTES);
    wifiHomeItem_.setCallback(main_screen_wifihome);
    addItem(wifiHomeItem_);
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
    wifiHomeItem_.trigger();
}
