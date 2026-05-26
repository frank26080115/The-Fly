#include "ScrollView.h"

#include "../../BluetoothManager/BtHostList.h"
#include "../../FlyGui/FlyGuiText.h"
#include "../../WifiManager/WifiManager.h"
#include "../ModalDialog.h"
#include "sprites.h"
#include <new>
#include <string.h>

extern ModalDialog* get_modal_dialog();

namespace
{
constexpr int16_t kItemWidth      = 106;
constexpr int16_t kItemHeight     = 114;
constexpr int16_t kItemY          = 20;
constexpr int16_t kTextY          = 140;
constexpr int16_t kTextMaxY       = 188;
constexpr int16_t kTextLineHeight = 17;
constexpr int16_t kExitSize       = 50;
constexpr int16_t kExitY          = 190;
constexpr float   kTextSize       = 1.0f;
constexpr uint8_t kTextFont       = 2;
constexpr uint32_t kBluetoothDeleteLongPressMs = 3000;

int32_t list_callback_value(size_t index)
{
    return static_cast<int32_t>(index);
}

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

sprite_desc_t make_sprite(const uint8_t* data, uint32_t width, uint32_t height, size_t byte_cnt)
{
    return { data, width, height, byte_cnt };
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

void draw_centered_wrapped_text(const char* text)
{
    if (!text || text[0] == '\0')
    {
        return;
    }

    constexpr size_t kLineMax = 128;
    constexpr size_t kNoSpace = static_cast<size_t>(-1);

    const int16_t width = static_cast<int16_t>(thefly_display.width() - 8);
    int16_t       y     = kTextY;
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
} // namespace

ScrollView* ScrollView::activeDeleteView_ = nullptr;

ScrollView::ScrollView(uint16_t viewId, FlyGuiItemCallback exitCallback)
    : FlyGuiView(viewId),
      exitItem_(exit_x(), kExitY, kExitSize, kExitSize),
      deleteItem_(delete_x(), kExitY, kExitSize, kExitSize),
      exitCallback_(exitCallback)
{
    activeDeleteView_ = this;
    exitItem_.setSprite(sprit_canceldoor_50, SPRIT_CANCELDOOR_50_WIDTH, SPRIT_CANCELDOOR_50_HEIGHT, SPRIT_CANCELDOOR_50_BYTES);
    exitItem_.setCallback(exitCallback_);

    deleteItem_.setSprite(sprit_trash_50, SPRIT_TRASH_50_WIDTH, SPRIT_TRASH_50_HEIGHT, SPRIT_TRASH_50_BYTES);
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

    bool ok = appendSpriteScrollItem(SCROLL_ITEM_BLUETOOTH_SHOW_SELF_INFO,
                                     SCROLL_TASK_BLUETOOTH_SHOW_SELF_INFO,
                                     "Bluetooth Info",
                                     make_sprite(sprit_info_100, SPRIT_INFO_100_WIDTH, SPRIT_INFO_100_HEIGHT, SPRIT_INFO_100_BYTES));

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
                                      IconLookup::ICON_CONTEXT_BLUETOOTH) && ok;
        }
    }

    ok = appendSpriteScrollItem(SCROLL_ITEM_BLUETOOTH_PAIRING,
                                SCROLL_TASK_BLUETOOTH_PAIRING,
                                "Pair New Device",
                                make_sprite(sprit_btpairing_100, SPRIT_BTPAIRING_100_WIDTH, SPRIT_BTPAIRING_100_HEIGHT, SPRIT_BTPAIRING_100_BYTES)) && ok;
    return ok;
}

bool ScrollView::populateWifi(const WifiManager* wifiManager)
{
    context_           = CONTEXT_WIFI;
    bluetoothHostList_ = nullptr;
    exitDeleteMode();
    clearGeneratedItems();

    bool ok = appendSpriteScrollItem(SCROLL_ITEM_WIFI_SCAN_AND_CONNECT,
                                     SCROLL_TASK_WIFI_SCAN_AND_CONNECT,
                                     "Scan and Connect",
                                     make_sprite(sprit_wifisearch_100, SPRIT_WIFISEARCH_100_WIDTH, SPRIT_WIFISEARCH_100_HEIGHT, SPRIT_WIFISEARCH_100_BYTES));

    if (wifiManager)
    {
        for (size_t i = 0; i < wifiManager->stationCount(); ++i)
        {
            const wifi_item_t* station = wifiManager->station(i);
            if (!station)
            {
                continue;
            }

            ok = appendIconScrollItem(SCROLL_ITEM_WIFI_STATION,
                                      list_callback_value(i),
                                      station->ssid,
                                      station->icon,
                                      IconLookup::ICON_CONTEXT_WIFI) && ok;
        }

        for (size_t i = 0; i < wifiManager->accessPointCount(); ++i)
        {
            const wifi_item_t* accessPoint = wifiManager->accessPoint(i);
            if (!accessPoint)
            {
                continue;
            }

            ok = appendIconScrollItem(SCROLL_ITEM_WIFI_AP,
                                      list_callback_value(i),
                                      accessPoint->ssid,
                                      accessPoint->icon,
                                      IconLookup::ICON_CONTEXT_WIFI) && ok;
        }
    }

    return ok;
}

bool ScrollView::populateCloud(const WifiManager* wifiManager)
{
    context_           = CONTEXT_CLOUD;
    bluetoothHostList_ = nullptr;
    exitDeleteMode();
    clearGeneratedItems();

    bool ok = appendSpriteScrollItem(SCROLL_ITEM_WIFI_SHOW_SELF_INFO,
                                     SCROLL_TASK_WIFI_SHOW_SELF_INFO,
                                     "Wi-Fi Info",
                                     make_sprite(sprit_wifi_100, SPRIT_WIFI_100_WIDTH, SPRIT_WIFI_100_HEIGHT, SPRIT_WIFI_100_BYTES));

    if (wifiManager)
    {
        for (size_t i = 0; i < wifiManager->cloudEndpointCount(); ++i)
        {
            const cloud_item_t* endpoint = wifiManager->cloudEndpoint(i);
            if (!endpoint)
            {
                continue;
            }

            ok = appendIconScrollItem(SCROLL_ITEM_CLOUD_ENDPOINT,
                                      list_callback_value(i),
                                      endpoint->url,
                                      endpoint->icon,
                                      IconLookup::ICON_CONTEXT_CLOUD) && ok;
        }
    }

    ok = appendSpriteScrollItem(SCROLL_ITEM_NTP_SYNC,
                                SCROLL_TASK_NTP_SYNC,
                                "Sync Time",
                                make_sprite(sprit_ntpsync_100, SPRIT_NTPSYNC_100_WIDTH, SPRIT_NTPSYNC_100_HEIGHT, SPRIT_NTPSYNC_100_BYTES)) && ok;
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

FlyGuiItem* ScrollView::selectedItem() const
{
    return itemAt(selectedIndex_);
}

void ScrollView::setExitCallback(FlyGuiItemCallback exitCallback)
{
    exitCallback_ = exitCallback;
    exitItem_.setCallback(exitCallback_);
}

void ScrollView::selectIndex(size_t index)
{
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
        return selected->handleTouch(event);
    }

    if (event.justPressed && containsSlot(SLOT_LEFT, event.x, event.y))
    {
        scrollLeft();
        return true;
    }

    if (event.justPressed && containsSlot(SLOT_RIGHT, event.x, event.y))
    {
        scrollRight();
        return true;
    }

    return false;
}

void ScrollView::redraw(bool forced)
{
    const bool visibleDeleteItemDirty = deleteMode_ && deleteItem_.dirty();
    if (!forced && !dirty() && !exitItem_.dirty() && !visibleDeleteItemDirty)
    {
        return;
    }

    drawContent();
    markClean();
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
    const FlyGuiItem* item = selectedItem();
    const ScrollItem* scrollItem = generatedScrollItemFor(item);
    const char*       text = scrollItem ? scrollItem->label() : (item ? item->mainText() : nullptr);
    if (!text || text[0] == '\0')
    {
        return;
    }

    thefly_display.fillRect(0, kTextY, thefly_display.width(), static_cast<int16_t>(kTextMaxY - kTextY), TFT_BLACK);
    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextDatum(top_center);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);
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

bool ScrollView::appendIconScrollItem(ScrollItemKind kind, int32_t callbackValue, const char* label, uint8_t icon, IconLookup::IconContext iconContext)
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

bool ScrollView::appendSpriteScrollItem(ScrollItemKind kind, int32_t callbackValue, const char* label, const sprite_desc_t& sprite)
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

void ScrollView::clearGeneratedItems()
{
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

void ScrollView::enterBluetoothDeleteMode(int32_t hostIndex)
{
    if (context_ != CONTEXT_BLUETOOTH || hostIndex < 0)
    {
        return;
    }

    deleteMode_      = true;
    deleteHostIndex_ = hostIndex;
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
    deleteItem_.setDirty();
    setDirty();
}

bool ScrollView::deleteArmedBluetoothHost()
{
    if (context_ != CONTEXT_BLUETOOTH || !deleteMode_ || deleteHostIndex_ < 0 || !bluetoothHostList_)
    {
        return false;
    }

    const size_t hostIndex = static_cast<size_t>(deleteHostIndex_);
    const bt_host_item_t* host = bluetoothHostList_->get(hostIndex);
    char hostName[96] = {};
    const char* displayName = bt_host_display_name(host);
    strncpy(hostName, displayName[0] != '\0' ? displayName : "Bluetooth host", sizeof(hostName) - 1);
    hostName[sizeof(hostName) - 1] = '\0';

    const bool unpaired = bluetoothHostList_->unpair(hostIndex);
    bool       ok       = unpaired;
    if (unpaired)
    {
        ok = bluetoothHostList_->saveToMicroSd(true);
    }

    exitDeleteMode();

    if (unpaired)
    {
        populateBluetooth(bluetoothHostList_);
    }

    ModalDialog* dialog = get_modal_dialog();
    FlyGui*      owner  = gui();
    if (dialog && owner)
    {
        char message[160];
        if (ok)
        {
            snprintf(message, sizeof(message), "%s\nhas been un-paired", hostName);
            activeDeleteView_ = this;
            dialog->configure(sprit_thumbsup_100,
                              SPRIT_THUMBSUP_100_BYTES,
                              SPRIT_THUMBSUP_100_WIDTH,
                              SPRIT_THUMBSUP_100_HEIGHT,
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
            dialog->configure(sprit_warning_100,
                              SPRIT_WARNING_100_BYTES,
                              SPRIT_WARNING_100_WIDTH,
                              SPRIT_WARNING_100_HEIGHT,
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
