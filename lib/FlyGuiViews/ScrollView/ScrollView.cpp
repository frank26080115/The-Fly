#include "ScrollView.h"

#include "../../AudioFileRecorder/MostRecentFiles.h"
#include "../../BluetoothManager/BtHostList.h"
#include "../../FlyGui/FlyGuiText.h"
#include "../../HapticsWrapper/HapticsWrapper.h"
#include "../../WifiManager/WifiManager.h"
#include "../ModalDialog.h"
#include "FileScrollItem.h"
#include "sprites.h"
#include "esp_system.h"
#include "utilfuncs.h"
#include <new>
#include <string.h>

extern ModalDialog* get_view_modal_dialog();

namespace
{
constexpr int16_t  kItemWidth                  = 106;
constexpr int16_t  kItemHeight                 = 114;
constexpr int16_t  kItemY                      = 20;
constexpr int16_t  kTextY                      = 140;
constexpr int16_t  kTextMaxY                   = 188;
constexpr int16_t  kTextLineHeight             = 17;
constexpr int16_t  kExitSize                   = 50;
constexpr int16_t  kExitY                      = 190;
constexpr float    kTextSize                   = 1.0f;
constexpr uint8_t  kTextFont                   = 2;
constexpr uint8_t  kSmallTextFont              = 1;
constexpr uint16_t kInsecureUrlTextColor       = TFT_ORANGE;
constexpr uint32_t kBluetoothDeleteLongPressMs = 3000;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static int32_t       list_callback_value(size_t index);
static sprite_desc_t make_sprite(const uint8_t* data, uint32_t width, uint32_t height, size_t byte_cnt);
static void          draw_centered_fitted_cloud_url(const char* text);
static void          draw_centered_wrapped_text(const char* text);
static void          draw_centered_line(char* line, size_t& len, int16_t& y);
static void          truncate_to_width(char* text, size_t text_size, int16_t width);
static const char*   display_cloud_url(const char* text);
static bool          is_secure_url(const char* text);
static bool          starts_with_case_insensitive(const char* text, const char* prefix);
static int16_t       slot_x(int slot);
static int16_t       exit_x();
static int16_t       delete_x();
} // namespace

ScrollView* ScrollView::activeDeleteView_ = nullptr;

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

ScrollView::ScrollView(uint16_t viewId, FlyGuiItemCallback exitCallback)
    : FlyGuiView(viewId), exitItem_(exit_x(), kExitY, kExitSize, kExitSize),
      deleteItem_(delete_x(), kExitY, kExitSize, kExitSize), exitCallback_(exitCallback)
{
    activeDeleteView_ = this;
    exitItem_.setSprite(sprite_doorreturn_50,
                        SPRITE_DOORRETURN_50_WIDTH,
                        SPRITE_DOORRETURN_50_HEIGHT,
                        SPRITE_DOORRETURN_50_BYTES);
    exitItem_.setCallback(exitCallback_);

    deleteItem_.setSprite(sprite_trash_50, SPRITE_TRASH_50_WIDTH, SPRITE_TRASH_50_HEIGHT, SPRITE_TRASH_50_BYTES);
    deleteItem_.setCallback(onDeleteButtonTriggered);
}

ScrollView::~ScrollView()
{
    clearGeneratedItems();
}

void ScrollView::addItem(FlyGuiItem& item)
{
    FlyGuiView::addItem(item);
    ++itemCount_;
    setDirty();
}

void ScrollView::removeAllItems()
{
    clearGeneratedItems();
}

void ScrollView::onUnload()
{
    exitDeleteMode();
    FlyGuiView::onUnload();
}

bool ScrollView::populateBluetooth(BtHostList* hostList)
{
    context_           = CONTEXT_BLUETOOTH;
    bluetoothHostList_ = hostList;
    exitDeleteMode();
    clearGeneratedItems();

    bool ok = appendSpriteScrollItem(
        SCROLL_ITEM_BLUETOOTH_SHOW_SELF_INFO,
        SCROLL_TASK_BLUETOOTH_SHOW_SELF_INFO,
        "Bluetooth Info",
        make_sprite(sprite_info_100, SPRITE_INFO_100_WIDTH, SPRITE_INFO_100_HEIGHT, SPRITE_INFO_100_BYTES));

    if (hostList)
    {
        for (size_t i = 0; i < hostList->size(); ++i)
        {
            const bt_host_item_t* host = hostList->get(i);
            if (!host)
            {
                continue;
            }

            if (!host->bonded)
            {
                continue;
            }

            ok = appendIconScrollItem(SCROLL_ITEM_BLUETOOTH_HOST,
                                      list_callback_value(i),
                                      bt_host_display_name(host),
                                      host->icon,
                                      IconLookup::ICON_CONTEXT_BLUETOOTH) &&
                 ok;
        }
    }

    ok = appendSpriteScrollItem(SCROLL_ITEM_BLUETOOTH_PAIRING,
                                SCROLL_TASK_BLUETOOTH_PAIRING,
                                "Pair New Device",
                                make_sprite(sprite_btpairing_100,
                                            SPRITE_BTPAIRING_100_WIDTH,
                                            SPRITE_BTPAIRING_100_HEIGHT,
                                            SPRITE_BTPAIRING_100_BYTES)) &&
         ok;
    return ok;
}

bool ScrollView::populateWifi()
{
    context_           = CONTEXT_WIFI;
    bluetoothHostList_ = nullptr;
    exitDeleteMode();
    clearGeneratedItems();

    bool ok = appendSpriteScrollItem(SCROLL_ITEM_WIFI_SCAN_AND_CONNECT,
                                     SCROLL_TASK_WIFI_SCAN_AND_CONNECT,
                                     "Scan and Connect",
                                     make_sprite(sprite_wifisearch_100,
                                                 SPRITE_WIFISEARCH_100_WIDTH,
                                                 SPRITE_WIFISEARCH_100_HEIGHT,
                                                 SPRITE_WIFISEARCH_100_BYTES));

    ok = appendSpriteScrollItem(SCROLL_ITEM_WIFI_AP,
                                SCROLL_TASK_WIFI_GENERATED_AP,
                                "Default Secure AP",
                                make_sprite(sprite_default_wifi_ap,
                                            SPRITE_DEFAULT_WIFI_AP_WIDTH,
                                            SPRITE_DEFAULT_WIFI_AP_HEIGHT,
                                            SPRITE_DEFAULT_WIFI_AP_BYTES)) &&
         ok;

    for (size_t i = 0; i < WifiManager::stationCount(); ++i)
    {
        const wifi_item_t* station = WifiManager::station(i);
        if (!station)
        {
            continue;
        }

        ok = appendIconScrollItem(SCROLL_ITEM_WIFI_STATION,
                                  list_callback_value(i),
                                  station->ssid,
                                  station->icon,
                                  IconLookup::ICON_CONTEXT_WIFI) &&
             ok;
    }

    for (size_t i = 0; i < WifiManager::accessPointCount(); ++i)
    {
        const wifi_item_t* accessPoint = WifiManager::accessPoint(i);
        if (!accessPoint)
        {
            continue;
        }

        ok = appendIconScrollItem(SCROLL_ITEM_WIFI_AP,
                                  list_callback_value(i),
                                  accessPoint->ssid,
                                  accessPoint->icon,
                                  IconLookup::ICON_CONTEXT_WIFI) &&
             ok;
    }

    return ok;
}

bool ScrollView::populateCloud()
{
    context_           = CONTEXT_CLOUD;
    bluetoothHostList_ = nullptr;
    exitDeleteMode();
    clearGeneratedItems();

    bool ok = appendSpriteScrollItem(
        SCROLL_ITEM_WIFI_SHOW_SELF_INFO,
        SCROLL_TASK_WIFI_SHOW_SELF_INFO,
        "Wi-Fi Info",
        make_sprite(sprite_wifiinfo_100, SPRITE_WIFIINFO_100_WIDTH, SPRITE_WIFIINFO_100_HEIGHT, SPRITE_WIFIINFO_100_BYTES));

#ifdef BUILD_CLOUD_FEATURES
    for (size_t i = 0; i < WifiManager::cloudEndpointCount(); ++i)
    {
        const cloud_item_t* endpoint = WifiManager::cloudEndpoint(i);
        if (!endpoint)
        {
            continue;
        }

        ok = appendIconScrollItem(SCROLL_ITEM_CLOUD_ENDPOINT,
                                  list_callback_value(i),
                                  endpoint->url,
                                  endpoint->icon,
                                  IconLookup::ICON_CONTEXT_CLOUD) &&
             ok;
    }
#else
    ok = appendSpriteScrollItem(SCROLL_ITEM_CLOUD_UNIMPLEMENTED,
                                SCROLL_TASK_CLOUD_UNIMPLEMENTED,
                                "Cloud Upload",
                                make_sprite(sprite_cloudupload_100,
                                            SPRITE_CLOUDUPLOAD_100_WIDTH,
                                            SPRITE_CLOUDUPLOAD_100_HEIGHT,
                                            SPRITE_CLOUDUPLOAD_100_BYTES)) &&
         ok;
#endif

    ok = appendSpriteScrollItem(SCROLL_ITEM_NTP_SYNC,
                                SCROLL_TASK_NTP_SYNC,
                                "Sync Time",
                                make_sprite(sprite_ntpsync_100,
                                            SPRITE_NTPSYNC_100_WIDTH,
                                            SPRITE_NTPSYNC_100_HEIGHT,
                                            SPRITE_NTPSYNC_100_BYTES)) &&
         ok;
    return ok;
}

bool ScrollView::populateFiles()
{
    context_           = CONTEXT_FILE_LIST;
    bluetoothHostList_ = nullptr;
    exitDeleteMode();
    clearGeneratedItems();

    bool ok = appendSpriteScrollItem(
        SCROLL_ITEM_FILE_SHOW_INFO,
        SCROLL_TASK_FILE_SHOW_INFO,
        "Storage Info",
        make_sprite(sprite_info_100, SPRITE_INFO_100_WIDTH, SPRITE_INFO_100_HEIGHT, SPRITE_INFO_100_BYTES));

    MostRecentFiles::FileList files = MostRecentFiles::get();
    for (size_t i = 0; i < files.count; ++i)
    {
        const char* file_name = files[i];
        if (!file_name || file_name[0] == '\0')
        {
            continue;
        }

        ScrollItemKind kind           = SCROLL_ITEM_FILE_UNKNOWN;
        int32_t        callback_value = list_callback_value(i);
        sprite_desc_t  sprite         = make_sprite(sprite_fileunknown_100,
                                                    SPRITE_FILEUNKNOWN_100_WIDTH,
                                                    SPRITE_FILEUNKNOWN_100_HEIGHT,
                                                    SPRITE_FILEUNKNOWN_100_BYTES);
        bool           use_file_item  = false;

        if (equals_case_insensitive(file_name, "firmware.bin"))
        {
            kind           = SCROLL_ITEM_FILE_FIRMWARE;
            callback_value = SCROLL_TASK_FILE_FIRMWARE_UPDATE;
            sprite = make_sprite(sprite_fwupdate, SPRITE_FWUPDATE_WIDTH, SPRITE_FWUPDATE_HEIGHT, SPRITE_FWUPDATE_BYTES);
        }
        else if (ends_with_case_insensitive(file_name, ".wav"))
        {
            kind          = SCROLL_ITEM_FILE_WAV;
            use_file_item = true;
        }
        else if (ends_with_case_insensitive(file_name, ".mp3"))
        {
            kind          = SCROLL_ITEM_FILE_MP3;
            use_file_item = true;
        }
        else if (ends_with_case_insensitive(file_name, ".rec"))
        {
            kind          = SCROLL_ITEM_FILE_REC;
            use_file_item = true;
        }
        else if (ends_with_case_insensitive(file_name, ".fly"))
        {
            kind          = SCROLL_ITEM_FILE_FLY;
            use_file_item = true;
        }

        if (use_file_item)
        {
            ok = appendFileScrollItem(kind, callback_value, file_name, files.fileSize(i)) && ok;
        }
        else
        {
            ok = appendSpriteScrollItem(kind, callback_value, file_name, sprite) && ok;
        }
    }

    return ok;
}

void ScrollView::setOnClickBluetoothHost(ScrollViewClickCallback callback)
{
    onBluetoothHost_ = callback;
}

void ScrollView::setOnClickBluetoothPair(ScrollViewClickCallback callback)
{
    onBluetoothPair_ = callback;
}

void ScrollView::setOnClickWifiScanAndConnect(ScrollViewClickCallback callback)
{
    onWifiScanAndConnect_ = callback;
}

void ScrollView::setOnClickWifiStation(ScrollViewClickCallback callback)
{
    onWifiStation_ = callback;
}

void ScrollView::setOnClickWifiAp(ScrollViewClickCallback callback)
{
    onWifiAp_ = callback;
}

void ScrollView::setOnClickCloudUpload(ScrollViewClickCallback callback)
{
    onCloudUpload_ = callback;
}

void ScrollView::setOnClickNtpSync(ScrollViewClickCallback callback)
{
    onNtpSync_ = callback;
}

void ScrollView::setOnClickBtShowInfo(ScrollViewClickCallback callback)
{
    onBtShowInfo_ = callback;
}

void ScrollView::setOnClickWifiShowInfo(ScrollViewClickCallback callback)
{
    onWifiShowInfo_ = callback;
}

void ScrollView::setOnClickFilePlayable(ScrollViewClickCallback callback)
{
    onFilePlayable_ = callback;
}

void ScrollView::setOnClickFileListShowInfo(ScrollViewClickCallback callback)
{
    onFileListShowInfo_ = callback;
}

FlyGuiItem* ScrollView::selectedItem() const
{
    return itemAt(selectedIndex_);
}

const char* ScrollView::selectedItemLabel() const
{
    const FlyGuiItem* item       = selectedItem();
    const ScrollItem* scrollItem = generatedScrollItemFor(item);
    if (scrollItem)
    {
        return scrollItem->label();
    }

    return item && item->mainText() ? item->mainText() : "";
}

bool ScrollView::isWifiContext() const
{
    return context_ == CONTEXT_WIFI;
}

bool ScrollView::isCloudContext() const
{
    return context_ == CONTEXT_CLOUD;
}

bool ScrollView::isFileListContext() const
{
    return context_ == CONTEXT_FILE_LIST;
}

void ScrollView::setExitCallback(FlyGuiItemCallback exitCallback)
{
    exitCallback_ = exitCallback;
    exitItem_.setCallback(exitCallback_);
}

void ScrollView::selectIndex(size_t index)
{
    resetBluetoothDeleteHold();
    exitDeleteMode();
    if (itemCount_ == 0)
    {
        selectedIndex_ = 0;
        setDirty();
        return;
    }

    const size_t wrapped = index % itemCount_;
    if (selectedIndex_ != wrapped)
    {
        selectedIndex_ = wrapped;
        setDirty();
    }
}

void ScrollView::scrollLeft()
{
    if (itemCount_ <= 1)
    {
        return;
    }

    resetBluetoothDeleteHold();
    exitDeleteMode();
    selectedIndex_ = selectedIndex_ == 0 ? itemCount_ - 1 : selectedIndex_ - 1;
    setDirty();
}

void ScrollView::scrollRight()
{
    if (itemCount_ <= 1)
    {
        return;
    }

    resetBluetoothDeleteHold();
    exitDeleteMode();
    selectedIndex_ = (selectedIndex_ + 1) % itemCount_;
    setDirty();
}

void ScrollView::onLoad()
{
    selectedIndex_ = 0;
    for (FlyGuiItem* item = firstItem(); item; item = item->next())
    {
        item->onLoad();
    }
    exitItem_.onLoad();
    setDirty();
}

bool ScrollView::handleTouch(const FlyGuiTouchEvent& event)
{
    if (deleteMode_ && (containsDeleteButton(event.x, event.y) || deleteItem_.isPressed()))
    {
        return deleteItem_.handleTouch(event);
    }

    if (exitItem_.contains(event.x, event.y) || exitItem_.isPressed())
    {
        return exitItem_.handleTouch(event);
    }

    FlyGuiItem* selected = selectedItem();
    if (selected && (containsSlot(SLOT_CENTER, event.x, event.y) || selected->isPressed()))
    {
        const ScrollItem* scrollItem = generatedScrollItemFor(selected);
        if (updateBluetoothDeleteHold(event, scrollItem, containsSlot(SLOT_CENTER, event.x, event.y)))
        {
            return true;
        }
        return selected->handleTouch(event);
    }

    if (event.justPressed && containsSlot(SLOT_LEFT, event.x, event.y))
    {
        haptic_play_click();
        scrollLeft();
        return true;
    }

    if (event.justPressed && containsSlot(SLOT_RIGHT, event.x, event.y))
    {
        haptic_play_click();
        scrollRight();
        return true;
    }

    return false;
}

bool ScrollView::redraw(bool forced)
{
    const bool visibleDeleteItemDirty = deleteMode_ && deleteItem_.dirty();
    if (!forced && !dirty() && !exitItem_.dirty() && !visibleDeleteItemDirty)
    {
        return false;
    }

    drawContent();
    markClean();
    return true;
}

void ScrollView::onPressLeft()
{
    scrollLeft();
}

void ScrollView::onPressMid()
{
    exitItem_.trigger();
}

void ScrollView::onPressRight()
{
    if (deleteMode_)
    {
        deleteItem_.trigger();
        return;
    }

    scrollRight();
}

FlyGuiItem* ScrollView::itemAt(size_t index) const
{
    size_t current = 0;
    for (FlyGuiItem* item = firstItem(); item; item = item->next())
    {
        if (current == index)
        {
            return item;
        }
        ++current;
    }

    return nullptr;
}

FlyGuiItem* ScrollView::itemAtWrapped(int32_t index) const
{
    if (itemCount_ == 0)
    {
        return nullptr;
    }

    while (index < 0)
    {
        index += static_cast<int32_t>(itemCount_);
    }

    return itemAt(static_cast<size_t>(index) % itemCount_);
}

const ScrollItem* ScrollView::generatedScrollItemFor(const FlyGuiItem* item) const
{
    for (GeneratedItemNode* node = generatedHead_; node; node = node->next)
    {
        if (node->item == item)
        {
            return node->item;
        }
    }

    return nullptr;
}

bool ScrollView::containsSlot(Slot slot, int16_t x, int16_t y) const
{
    const int16_t sx = slot_x(slot);
    return x >= sx && y >= kItemY && x < sx + kItemWidth && y < kItemY + kItemHeight;
}

bool ScrollView::containsDeleteButton(int16_t x, int16_t y) const
{
    return x >= delete_x() && y >= kExitY && x < delete_x() + kExitSize && y < kExitY + kExitSize;
}

void ScrollView::drawContent()
{
    thefly_display.fillRect(0,
                            FlyGui::kTopBarHeight,
                            thefly_display.width(),
                            static_cast<int16_t>(thefly_display.height() - FlyGui::kTopBarHeight),
                            TFT_BLACK);

    if (itemCount_ > 0)
    {
        if (itemCount_ == 1)
        {
            FlyGuiItem* only = selectedItem();
            if (only)
            {
                drawItemInSlot(*only, SLOT_LEFT, true);
                drawItemInSlot(*only, SLOT_RIGHT, true);
            }
        }
        else if (itemCount_ == 2)
        {
            FlyGuiItem* neighbor = selectedIndex_ == 0 ? itemAt(1) : itemAt(0);
            if (neighbor)
            {
                drawItemInSlot(*neighbor, selectedIndex_ == 0 ? SLOT_RIGHT : SLOT_LEFT, true);
            }
        }
        else
        {
            FlyGuiItem* left = itemAtWrapped(static_cast<int32_t>(selectedIndex_) - 1);
            if (left)
            {
                drawItemInSlot(*left, SLOT_LEFT, true);
            }

            FlyGuiItem* right = itemAtWrapped(static_cast<int32_t>(selectedIndex_) + 1);
            if (right)
            {
                drawItemInSlot(*right, SLOT_RIGHT, true);
            }
        }

        FlyGuiItem* center = selectedItem();
        if (center)
        {
            drawItemInSlot(*center, SLOT_CENTER, false);
        }

        drawSelectedText();
    }

    drawExitButton();
    drawDeleteButton();
}

void ScrollView::drawItemInSlot(FlyGuiItem& item, Slot slot, bool faded)
{
    item.relocate(slot_x(slot), kItemY, kItemWidth, kItemHeight);
    item.setFaded(faded);
    item.redraw(true);
}

void ScrollView::drawSelectedText()
{
    const FlyGuiItem* item       = selectedItem();
    const ScrollItem* scrollItem = generatedScrollItemFor(item);
    const char*       text       = scrollItem ? scrollItem->label() : (item ? item->mainText() : nullptr);
    const char*       detail     = scrollItem ? scrollItem->detailText() : nullptr;
    if (!text || text[0] == '\0')
    {
        return;
    }

    thefly_display.fillRect(0, kTextY, thefly_display.width(), static_cast<int16_t>(kTextMaxY - kTextY), TFT_BLACK);
    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextDatum(top_center);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);

    if (scrollItem && scrollItem->kind() == SCROLL_ITEM_CLOUD_ENDPOINT)
    {
        draw_centered_fitted_cloud_url(text);
        return;
    }

    if (detail && detail[0] != '\0')
    {
        char detailedText[ScrollItem::kLabelCapacity + 40] = {};
        snprintf(detailedText, sizeof(detailedText), "%s\n%s", text, detail);
        draw_centered_wrapped_text(detailedText);
        return;
    }

    draw_centered_wrapped_text(text);
}

void ScrollView::drawExitButton()
{
    exitItem_.relocate(exit_x(), kExitY, kExitSize, kExitSize);
    exitItem_.redraw(true);
}

void ScrollView::drawDeleteButton()
{
    if (!deleteMode_)
    {
        deleteItem_.setDirty(false);
        return;
    }

    deleteItem_.relocate(delete_x(), kExitY, kExitSize, kExitSize);
    deleteItem_.redraw(true);
}

bool ScrollView::appendIconScrollItem(
    ScrollItemKind kind, int32_t callbackValue, const char* label, uint8_t icon, IconLookup::IconContext iconContext)
{
    ScrollItem* item = new (std::nothrow) ScrollItem();
    if (!item)
    {
        return false;
    }

    GeneratedItemNode* node = new (std::nothrow) GeneratedItemNode();
    if (!node)
    {
        delete item;
        return false;
    }

    item->configure(kind, callbackValue, label, icon, iconContext);
    item->setScrollCallback(onScrollItemTriggered, this);

    node->item = item;
    node->next = nullptr;
    if (generatedTail_)
    {
        generatedTail_->next = node;
    }
    else
    {
        generatedHead_ = node;
    }
    generatedTail_ = node;

    addItem(*item);
    return true;
}

bool ScrollView::appendSpriteScrollItem(ScrollItemKind       kind,
                                        int32_t              callbackValue,
                                        const char*          label,
                                        const sprite_desc_t& sprite)
{
    ScrollItem* item = new (std::nothrow) ScrollItem();
    if (!item)
    {
        return false;
    }

    GeneratedItemNode* node = new (std::nothrow) GeneratedItemNode();
    if (!node)
    {
        delete item;
        return false;
    }

    item->configureSprite(kind, callbackValue, label, sprite);
    item->setScrollCallback(onScrollItemTriggered, this);

    node->item = item;
    node->next = nullptr;
    if (generatedTail_)
    {
        generatedTail_->next = node;
    }
    else
    {
        generatedHead_ = node;
    }
    generatedTail_ = node;

    addItem(*item);
    return true;
}

bool ScrollView::appendFileScrollItem(ScrollItemKind kind, int32_t callbackValue, const char* fileName, uint64_t fileSize)
{
    FileScrollItem* item = new (std::nothrow) FileScrollItem();
    if (!item)
    {
        return false;
    }

    GeneratedItemNode* node = new (std::nothrow) GeneratedItemNode();
    if (!node)
    {
        delete item;
        return false;
    }

    item->configureFile(kind, callbackValue, fileName, fileSize);
    item->setScrollCallback(onScrollItemTriggered, this);

    node->item = item;
    node->next = nullptr;
    if (generatedTail_)
    {
        generatedTail_->next = node;
    }
    else
    {
        generatedHead_ = node;
    }
    generatedTail_ = node;

    addItem(*item);
    return true;
}

void ScrollView::clearGeneratedItems()
{
    resetBluetoothDeleteHold();
    FlyGuiView::removeAllItems();

    GeneratedItemNode* node = generatedHead_;
    while (node)
    {
        GeneratedItemNode* next = node->next;
        delete node->item;
        delete node;
        node = next;
    }

    generatedHead_ = nullptr;
    generatedTail_ = nullptr;
    itemCount_     = 0;
    selectedIndex_ = 0;
    setDirty();
}

bool ScrollView::updateBluetoothDeleteHold(const FlyGuiTouchEvent& event, const ScrollItem* item, bool centerHit)
{
    if (context_ != CONTEXT_BLUETOOTH || deleteMode_ || !item || item->kind() != SCROLL_ITEM_BLUETOOTH_HOST)
    {
        if (event.justReleased || !event.pressed)
        {
            resetBluetoothDeleteHold();
        }
        return false;
    }

    if (event.justPressed && centerHit)
    {
        deleteHoldActive_    = true;
        deleteHoldShown_     = false;
        deleteHoldHostIndex_ = item->callbackValue();
        deleteHoldStartMs_   = millis();
    }

    if (!event.pressed)
    {
        resetBluetoothDeleteHold();
        return false;
    }

    if (!deleteHoldActive_ || deleteHoldHostIndex_ != item->callbackValue())
    {
        return false;
    }

    if (!deleteHoldShown_ && static_cast<uint32_t>(millis() - deleteHoldStartMs_) >= kBluetoothDeleteLongPressMs)
    {
        deleteHoldShown_ = true;
        enterBluetoothDeleteMode(deleteHoldHostIndex_);
        return true;
    }

    return false;
}

void ScrollView::resetBluetoothDeleteHold()
{
    deleteHoldActive_    = false;
    deleteHoldShown_     = false;
    deleteHoldHostIndex_ = -1;
    deleteHoldStartMs_   = 0;
}

void ScrollView::enterBluetoothDeleteMode(int32_t hostIndex)
{
    if (context_ != CONTEXT_BLUETOOTH || hostIndex < 0)
    {
        return;
    }

    deleteMode_      = true;
    deleteHostIndex_ = hostIndex;
    deleteHoldShown_ = true;
    haptic_play_click();
    deleteItem_.setDirty();
    setDirty();
}

void ScrollView::exitDeleteMode()
{
    if (!deleteMode_ && deleteHostIndex_ < 0)
    {
        return;
    }

    deleteMode_      = false;
    deleteHostIndex_ = -1;
    resetBluetoothDeleteHold();
    deleteItem_.setDirty();
    setDirty();
}

bool ScrollView::deleteArmedBluetoothHost()
{
    if (context_ != CONTEXT_BLUETOOTH || !deleteMode_ || deleteHostIndex_ < 0 || !bluetoothHostList_)
    {
        return false;
    }

    const size_t          hostIndex    = static_cast<size_t>(deleteHostIndex_);
    const bt_host_item_t* host         = bluetoothHostList_->get(hostIndex);
    char                  hostName[96] = {};
    const char*           displayName  = bt_host_display_name(host);
    strncpy(hostName, displayName[0] != '\0' ? displayName : "Bluetooth host", sizeof(hostName) - 1);
    hostName[sizeof(hostName) - 1] = '\0';

    const bool unpaired = bluetoothHostList_->unpair(hostIndex);
    bool       ok       = unpaired;

    exitDeleteMode();

    if (unpaired)
    {
        populateBluetooth(bluetoothHostList_);
    }

    ModalDialog* dialog = get_view_modal_dialog();
    FlyGui*      owner  = gui();
    if (dialog && owner)
    {
        char message[160];
        if (ok)
        {
            snprintf(message, sizeof(message), "%s\nhas been un-paired", hostName);
            activeDeleteView_ = this;
            dialog->configure(sprite_thumbsup_100,
                              SPRITE_THUMBSUP_100_BYTES,
                              SPRITE_THUMBSUP_100_WIDTH,
                              SPRITE_THUMBSUP_100_HEIGHT,
                              message,
                              FLYGUI_VIEW_SCROLL,
                              onBluetoothDeleteDialogDismissed);
        }
        else
        {
            snprintf(message,
                     sizeof(message),
                     "Bluetooth un-pair failed\n%s",
                     bluetoothHostList_ ? bluetoothHostList_->lastLoadResultName() : "No host list");
            dialog->configure(sprite_warning_100,
                              SPRITE_WARNING_100_BYTES,
                              SPRITE_WARNING_100_WIDTH,
                              SPRITE_WARNING_100_HEIGHT,
                              message,
                              FLYGUI_VIEW_SCROLL);
        }
        owner->showView(FLYGUI_VIEW_MODAL_DIALOG);
    }

    return ok;
}

void ScrollView::handleScrollItem(ScrollItem& item, uint32_t pressDurationMs)
{
    const int32_t value = item.callbackValue();

    if (item.kind() == SCROLL_ITEM_BLUETOOTH_HOST && pressDurationMs >= kBluetoothDeleteLongPressMs)
    {
        enterBluetoothDeleteMode(value);
        return;
    }

    if (deleteMode_)
    {
        exitDeleteMode();
    }

    switch (item.kind())
    {
    case SCROLL_ITEM_BLUETOOTH_HOST:
        if (onBluetoothHost_)
        {
            onBluetoothHost_(value, pressDurationMs);
        }
        break;
    case SCROLL_ITEM_BLUETOOTH_PAIRING:
        if (onBluetoothPair_)
        {
            onBluetoothPair_(value, pressDurationMs);
        }
        break;
    case SCROLL_ITEM_BLUETOOTH_SHOW_SELF_INFO:
        if (onBtShowInfo_)
        {
            onBtShowInfo_(value, pressDurationMs);
        }
        break;
    case SCROLL_ITEM_WIFI_SCAN_AND_CONNECT:
        if (onWifiScanAndConnect_)
        {
            onWifiScanAndConnect_(value, pressDurationMs);
        }
        break;
    case SCROLL_ITEM_WIFI_STATION:
        if (onWifiStation_)
        {
            onWifiStation_(value, pressDurationMs);
        }
        break;
    case SCROLL_ITEM_WIFI_AP:
        if (onWifiAp_)
        {
            onWifiAp_(value, pressDurationMs);
        }
        break;
    case SCROLL_ITEM_CLOUD_ENDPOINT:
        if (onCloudUpload_)
        {
            onCloudUpload_(value, pressDurationMs);
        }
        break;
    case SCROLL_ITEM_CLOUD_UNIMPLEMENTED:
#ifndef BUILD_CLOUD_FEATURES
    {
        ModalDialog* dialog = get_view_modal_dialog();
        FlyGui*      owner  = gui();
        if (dialog && owner)
        {
            dialog->configure(sprite_cloudupload_100,
                              SPRITE_CLOUDUPLOAD_100_BYTES,
                              SPRITE_CLOUDUPLOAD_100_WIDTH,
                              SPRITE_CLOUDUPLOAD_100_HEIGHT,
                              "Cloud upload is an unimplemented future feature.",
                              FLYGUI_VIEW_SCROLL);
            owner->showView(FLYGUI_VIEW_MODAL_DIALOG);
        }
    }
#endif
        break;
    case SCROLL_ITEM_NTP_SYNC:
        if (onNtpSync_)
        {
            onNtpSync_(value, pressDurationMs);
        }
        break;
    case SCROLL_ITEM_WIFI_SHOW_SELF_INFO:
        if (onWifiShowInfo_)
        {
            onWifiShowInfo_(value, pressDurationMs);
        }
        break;
    case SCROLL_ITEM_FILE_WAV:
    case SCROLL_ITEM_FILE_MP3:
        if (onFilePlayable_)
        {
            onFilePlayable_(value, pressDurationMs);
        }
        break;
    case SCROLL_ITEM_FILE_REC:
    case SCROLL_ITEM_FILE_FLY:
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
        if (onFilePlayable_)
        {
            onFilePlayable_(value, pressDurationMs);
        }
#endif
        break;
    case SCROLL_ITEM_FILE_FIRMWARE:
        FlyGui::quickScreenFade();
        delay(50);
        esp_restart();
        break;
    case SCROLL_ITEM_FILE_SHOW_INFO:
        if (onFileListShowInfo_)
        {
            onFileListShowInfo_(value, pressDurationMs);
        }
        break;
    default:
        break;
    }
}

void ScrollView::onScrollItemTriggered(ScrollItem& item, void* context, uint32_t pressDurationMs)
{
    if (context)
    {
        static_cast<ScrollView*>(context)->handleScrollItem(item, pressDurationMs);
    }
}

void ScrollView::onDeleteButtonTriggered(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    if (activeDeleteView_)
    {
        activeDeleteView_->deleteArmedBluetoothHost();
    }
}

void ScrollView::onBluetoothDeleteDialogDismissed()
{
    if (activeDeleteView_ && activeDeleteView_->context_ == CONTEXT_BLUETOOTH)
    {
        activeDeleteView_->populateBluetooth(activeDeleteView_->bluetoothHostList_);
    }
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

int32_t list_callback_value(size_t index)
{
    return static_cast<int32_t>(index);
}

sprite_desc_t make_sprite(const uint8_t* data, uint32_t width, uint32_t height, size_t byte_cnt)
{
    return {data, width, height, byte_cnt};
}

void draw_centered_fitted_cloud_url(const char* text)
{
    if (!text || text[0] == '\0')
    {
        return;
    }

    const bool    secure = is_secure_url(text);
    const char*   shown  = display_cloud_url(text);
    const int16_t width  = static_cast<int16_t>(thefly_display.width() - 8);

    thefly_display.setTextColor(secure ? TFT_WHITE : kInsecureUrlTextColor, TFT_BLACK);
    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    if (thefly_display.textWidth(shown) <= width)
    {
        thefly_display.drawString(shown, thefly_display.width() / 2, kTextY);
        return;
    }

    thefly_display.setTextFont(kSmallTextFont);
    thefly_display.setTextSize(kTextSize);
    if (thefly_display.textWidth(shown) <= width)
    {
        thefly_display.drawString(shown, thefly_display.width() / 2, kTextY);
        return;
    }

    char truncated[ScrollItem::kLabelCapacity] = {};
    strlcpy(truncated, shown, sizeof(truncated));
    truncate_to_width(truncated, sizeof(truncated), width);
    thefly_display.drawString(truncated, thefly_display.width() / 2, kTextY);
}

void draw_centered_wrapped_text(const char* text)
{
    if (!text || text[0] == '\0')
    {
        return;
    }

    constexpr size_t kLineMax = 128;
    constexpr size_t kNoSpace = static_cast<size_t>(-1);

    const int16_t width          = static_cast<int16_t>(thefly_display.width() - 8);
    int16_t       y              = kTextY;
    char          line[kLineMax] = {};
    size_t        len            = 0;
    size_t        lastSpace      = kNoSpace;

    for (const char* p = text; *p; ++p)
    {
        const char c = *p;
        if (c == '\r')
        {
            continue;
        }

        if (c == '\n')
        {
            draw_centered_line(line, len, y);
            lastSpace = kNoSpace;
            continue;
        }

        if (len + 1 >= sizeof(line))
        {
            draw_centered_line(line, len, y);
            lastSpace = kNoSpace;
        }

        line[len++] = c;
        line[len]   = '\0';
        if (c == ' ')
        {
            lastSpace = len - 1;
        }

        while (thefly_display.textWidth(line) > width && len > 1)
        {
            if (lastSpace != kNoSpace && lastSpace > 0)
            {
                char remainder[kLineMax] = {};
                strncpy(remainder, line + lastSpace + 1, sizeof(remainder) - 1);
                line[lastSpace] = '\0';
                size_t drawLen  = lastSpace;
                draw_centered_line(line, drawLen, y);
                strncpy(line, remainder, sizeof(line) - 1);
                line[sizeof(line) - 1] = '\0';
                len                    = strlen(line);
                lastSpace              = kNoSpace;
                for (size_t i = 0; i < len; ++i)
                {
                    if (line[i] == ' ')
                    {
                        lastSpace = i;
                    }
                }
            }
            else
            {
                const char overflow = line[len - 1];
                line[len - 1]       = '\0';
                size_t drawLen      = len - 1;
                draw_centered_line(line, drawLen, y);
                line[0]   = overflow;
                line[1]   = '\0';
                len       = 1;
                lastSpace = overflow == ' ' ? 0 : kNoSpace;
            }
        }
    }

    if (len > 0)
    {
        draw_centered_line(line, len, y);
    }
}

void draw_centered_line(char* line, size_t& len, int16_t& y)
{
    while (len > 0 && line[0] == ' ')
    {
        memmove(line, line + 1, len);
        --len;
        line[len] = '\0';
    }

    while (len > 0 && line[len - 1] == ' ')
    {
        line[--len] = '\0';
    }

    if (len == 0 || y + kTextLineHeight > kTextMaxY)
    {
        return;
    }

    thefly_display.drawString(line, thefly_display.width() / 2, y);
    y += kTextLineHeight;
    len     = 0;
    line[0] = '\0';
}

// -----------------------------------------------------------------------------
// Formatting Helpers
// -----------------------------------------------------------------------------

void truncate_to_width(char* text, size_t text_size, int16_t width)
{
    static constexpr const char* kEllipsis    = "...";
    static constexpr size_t      kEllipsisLen = 3;

    if (!text || text_size <= kEllipsisLen + 1 || thefly_display.textWidth(text) <= width)
    {
        return;
    }

    size_t len = strlen(text);
    while (len > 0)
    {
        --len;
        text[len] = '\0';
        strncat(text, kEllipsis, text_size - strlen(text) - 1);
        if (thefly_display.textWidth(text) <= width)
        {
            return;
        }
        text[len] = '\0';
    }

    strlcpy(text, kEllipsis, text_size);
}

const char* display_cloud_url(const char* text)
{
    if (starts_with_case_insensitive(text, "https://www."))
    {
        return text + 12;
    }
    if (starts_with_case_insensitive(text, "http://www."))
    {
        return text + 11;
    }
    if (starts_with_case_insensitive(text, "https://"))
    {
        return text + 8;
    }
    if (starts_with_case_insensitive(text, "http://"))
    {
        return text + 7;
    }

    return text;
}

bool is_secure_url(const char* text)
{
    return starts_with_case_insensitive(text, "https://");
}

bool starts_with_case_insensitive(const char* text, const char* prefix)
{
    if (!text || !prefix)
    {
        return false;
    }

    while (*prefix)
    {
        char lhs = *text++;
        char rhs = *prefix++;
        if (lhs >= 'A' && lhs <= 'Z')
        {
            lhs = static_cast<char>(lhs - 'A' + 'a');
        }
        if (rhs >= 'A' && rhs <= 'Z')
        {
            rhs = static_cast<char>(rhs - 'A' + 'a');
        }
        if (lhs != rhs)
        {
            return false;
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

int16_t slot_x(int slot)
{
    switch (slot)
    {
    case 0:
        return 1;
    case 1:
        return static_cast<int16_t>((thefly_display.width() - kItemWidth) / 2);
    case 2:
    default:
        return static_cast<int16_t>(thefly_display.width() - kItemWidth - 1);
    }
}

int16_t exit_x()
{
    return static_cast<int16_t>((thefly_display.width() - kExitSize) / 2);
}

int16_t delete_x()
{
    return static_cast<int16_t>((thefly_display.width() * 5 / 6) - (kExitSize / 2));
}

} // namespace
