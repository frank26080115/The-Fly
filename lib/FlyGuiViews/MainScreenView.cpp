#include "MainScreenView.h"

#include "BtHostList.h"
#include "ClockAgent.h"
#include "SpriteDraw.h"
#include "main_callbacks.h"
#include "sprites.h"

extern BtHostList* bt_host_list;

namespace
{
constexpr int16_t kButtonWidth  = 106;
constexpr int16_t kButtonHeight = 114;
constexpr int16_t kGridX        = 1;
constexpr int16_t kGridY        = FlyGui::kTopBarHeight;
constexpr int16_t kSpriteSize   = 100;
constexpr int16_t kSpriteXInset = (kButtonWidth - kSpriteSize) / 2;
constexpr int16_t kSpriteYInset = (kButtonHeight - kSpriteSize) / 2;
constexpr int16_t kCol0         = kGridX + kSpriteXInset;
constexpr int16_t kCol1         = kGridX + kButtonWidth + kSpriteXInset;
constexpr int16_t kCol2         = kGridX + kButtonWidth * 2 + kSpriteXInset;
constexpr int16_t kRow0         = kGridY + kSpriteYInset;
constexpr int16_t kRow1         = kGridY + kButtonHeight + kSpriteYInset;
} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

MainScreenView::MainScreenView()
    : // Design: main screen lets the user choose bluetooth, files, memo, or hosts.
      FlyGuiView(FLYGUI_VIEW_MAIN), bluetoothItem_(kCol0, kRow0, kSpriteSize, kSpriteSize),
      filesItem_(kCol1, kRow0, kSpriteSize, kSpriteSize), memoItem_(kCol2, kRow0, kSpriteSize, kSpriteSize),
      smartphoneItem_(kCol0, kRow1, kSpriteSize, kSpriteSize), laptopItem_(kCol1, kRow1, kSpriteSize, kSpriteSize),
      wifiItem_(kCol2, kRow1, kSpriteSize, kSpriteSize)
{
    bluetoothItem_.setSprite(sprite_bluetooth_100,
                             SPRITE_BLUETOOTH_100_WIDTH,
                             SPRITE_BLUETOOTH_100_HEIGHT,
                             SPRITE_BLUETOOTH_100_BYTES);
    bluetoothItem_.setCallback(onclick_main_bluetooth);
    addItem(bluetoothItem_);

    syncFilesIcon();
    filesItem_.setCallback(onclick_main_files);
    addItem(filesItem_);

    memoItem_.setSprite(sprite_memo_100, SPRITE_MEMO_100_WIDTH, SPRITE_MEMO_100_HEIGHT, SPRITE_MEMO_100_BYTES);
    memoItem_.setCallback(onclick_main_memo);
    addItem(memoItem_);

    smartphoneItem_.setSprite(sprite_smartphone_bt_100,
                              SPRITE_SMARTPHONE_BT_100_WIDTH,
                              SPRITE_SMARTPHONE_BT_100_HEIGHT,
                              SPRITE_SMARTPHONE_BT_100_BYTES);
    smartphoneItem_.setCallback(onclick_main_smartphone);
    addItem(smartphoneItem_);

    laptopItem_.setSprite(sprite_laptop_bt_100,
                          SPRITE_LAPTOP_BT_100_WIDTH,
                          SPRITE_LAPTOP_BT_100_HEIGHT,
                          SPRITE_LAPTOP_BT_100_BYTES);
    laptopItem_.setCallback(onclick_main_laptop);
    addItem(laptopItem_);

    wifiItem_.setSprite(sprite_wifi_100, SPRITE_WIFI_100_WIDTH, SPRITE_WIFI_100_HEIGHT, SPRITE_WIFI_100_BYTES);
    wifiItem_.setCallback(onclick_main_wifi);
    addItem(wifiItem_);
}

void MainScreenView::onLoad()
{
    syncFilesIcon();
    syncBluetoothHostButtonFades();
    FlyGuiView::onLoad();
}

bool MainScreenView::redraw(bool forced)
{
    syncBluetoothHostButtonFades();
    return FlyGuiView::redraw(forced);
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

void MainScreenView::showMemoStartingFeedback()
{
    FlyGui::quickScreenFade();

    const int16_t hourglassX =
        memoItem_.x() +
        static_cast<int16_t>((memoItem_.width() - static_cast<int32_t>(SPRITE_HOURGLASS_30_OVERLAY_WIDTH)) / 2);
    const int16_t hourglassY =
        memoItem_.y() +
        static_cast<int16_t>((memoItem_.height() - static_cast<int32_t>(SPRITE_HOURGLASS_30_OVERLAY_HEIGHT)) / 2);
    SpriteDraw::drawPng(sprite_hourglass_30_overlay,
                        SPRITE_HOURGLASS_30_OVERLAY_BYTES,
                        hourglassX,
                        hourglassY,
                        SPRITE_HOURGLASS_30_OVERLAY_WIDTH,
                        SPRITE_HOURGLASS_30_OVERLAY_HEIGHT,
                        true);
}

void MainScreenView::syncFilesIcon()
{
    const m5::rtc_time_t now = Clock.getTime();
    switch (now.minutes % 6)
    {
    case 0:
        filesItem_.setSprite(
            sprite_floppy_100, SPRITE_FLOPPY_100_WIDTH, SPRITE_FLOPPY_100_HEIGHT, SPRITE_FLOPPY_100_BYTES);
        break;
    case 1:
        filesItem_.setSprite(sprite_microsdcard_100,
                             SPRITE_MICROSDCARD_100_WIDTH,
                             SPRITE_MICROSDCARD_100_HEIGHT,
                             SPRITE_MICROSDCARD_100_BYTES);
        break;
    case 2:
        filesItem_.setSprite(sprite_stackedfiles_100,
                             SPRITE_STACKEDFILES_100_WIDTH,
                             SPRITE_STACKEDFILES_100_HEIGHT,
                             SPRITE_STACKEDFILES_100_BYTES);
        break;
    case 3:
        filesItem_.setSprite(sprite_folderwfiles_100,
                             SPRITE_FOLDERWFILES_100_WIDTH,
                             SPRITE_FOLDERWFILES_100_HEIGHT,
                             SPRITE_FOLDERWFILES_100_BYTES);
        break;
    case 4:
        filesItem_.setSprite(sprite_harddrive_100,
                             SPRITE_HARDDRIVE_100_WIDTH,
                             SPRITE_HARDDRIVE_100_HEIGHT,
                             SPRITE_HARDDRIVE_100_BYTES);
        break;
    default:
        filesItem_.setSprite(sprite_filecabinet_100,
                             SPRITE_FILECABINET_100_WIDTH,
                             SPRITE_FILECABINET_100_HEIGHT,
                             SPRITE_FILECABINET_100_BYTES);
        break;
    }
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
