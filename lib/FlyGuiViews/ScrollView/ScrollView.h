#pragma once

#include "../../FlyGui/FlyGui.h"
#include "ScrollItem.h"
#include <stddef.h>

class BtHostList;
class WifiManager;

using ScrollViewClickCallback = void (*)(int32_t value, uint32_t pressDurationMs);

class ScrollView : public FlyGuiView
{
public:
    explicit ScrollView(uint16_t viewId = FLYGUI_VIEW_SCROLL, FlyGuiItemCallback exitCallback = nullptr);
    ~ScrollView() override;

    // Planned callers can populate this view from JSON-backed records by looking
    // up each record's icon through IconLookup, assigning that sprite to a
    // FlyGuiItem, and adding it here. Bluetooth can append known hosts plus
    // pairing/prune actions, Wi-Fi can append station/AP choices, and cloud can
    // append endpoint/NTP/info actions.
    void addItem(FlyGuiItem& item);
    void removeAllItems();
    bool populateBluetooth(BtHostList* hostList);
    bool populateWifi(const WifiManager* wifiManager);
    bool populateCloud(const WifiManager* wifiManager);
    bool populateFiles();

    void setOnClickBluetoothHost(ScrollViewClickCallback callback);
    void setOnClickBluetoothPair(ScrollViewClickCallback callback);
    void setOnClickWifiScanAndConnect(ScrollViewClickCallback callback);
    void setOnClickWifiStation(ScrollViewClickCallback callback);
    void setOnClickWifiAp(ScrollViewClickCallback callback);
    void setOnClickCloudUpload(ScrollViewClickCallback callback);
    void setOnClickNtpSync(ScrollViewClickCallback callback);
    void setOnClickBtShowInfo(ScrollViewClickCallback callback);
    void setOnClickWifiShowInfo(ScrollViewClickCallback callback);
    void setOnClickFilePlayable(ScrollViewClickCallback callback);
    void setOnClickFileListShowInfo(ScrollViewClickCallback callback);

    size_t itemCount() const
    {
        return itemCount_;
    }
    size_t selectedIndex() const
    {
        return selectedIndex_;
    }
    FlyGuiItem* selectedItem() const;
    const char* selectedItemLabel() const;
    bool        isWifiContext() const;
    bool        isCloudContext() const;
    bool        isFileListContext() const;

    void setExitCallback(FlyGuiItemCallback exitCallback);
    void selectIndex(size_t index);
    void scrollLeft();
    void scrollRight();

    void onLoad() override;
    void onUnload() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    void redraw(bool forced) override;
    void onPressLeft() override;
    void onPressMid() override;
    void onPressRight() override;

private:
    enum Context
    {
        CONTEXT_NONE,
        CONTEXT_BLUETOOTH,
        CONTEXT_WIFI,
        CONTEXT_CLOUD,
        CONTEXT_FILE_LIST,
    };

    enum Slot
    {
        SLOT_LEFT,
        SLOT_CENTER,
        SLOT_RIGHT,
    };

    FlyGuiItem*       itemAt(size_t index) const;
    FlyGuiItem*       itemAtWrapped(int32_t index) const;
    const ScrollItem* generatedScrollItemFor(const FlyGuiItem* item) const;
    bool              containsSlot(Slot slot, int16_t x, int16_t y) const;
    bool              containsDeleteButton(int16_t x, int16_t y) const;
    void              drawContent();
    void              drawItemInSlot(FlyGuiItem& item, Slot slot, bool faded);
    void              drawSelectedText();
    void              drawExitButton();
    void              drawDeleteButton();
    bool              appendIconScrollItem(ScrollItemKind          kind,
                                           int32_t                 callbackValue,
                                           const char*             label,
                                           uint8_t                 icon,
                                           IconLookup::IconContext iconContext);
    bool
    appendSpriteScrollItem(ScrollItemKind kind, int32_t callbackValue, const char* label, const sprite_desc_t& sprite);
    bool appendFileScrollItem(ScrollItemKind kind, int32_t callbackValue, const char* fileName);
    void        clearGeneratedItems();
    bool        updateBluetoothDeleteHold(const FlyGuiTouchEvent& event, const ScrollItem* item, bool centerHit);
    void        resetBluetoothDeleteHold();
    void        enterBluetoothDeleteMode(int32_t hostIndex);
    void        exitDeleteMode();
    bool        deleteArmedBluetoothHost();
    void        handleScrollItem(ScrollItem& item, uint32_t pressDurationMs);
    static void onScrollItemTriggered(ScrollItem& item, void* context, uint32_t pressDurationMs);
    static void onDeleteButtonTriggered(uint32_t pressDurationMs);
    static void onBluetoothDeleteDialogDismissed();

    struct GeneratedItemNode
    {
        ScrollItem*        item = nullptr;
        GeneratedItemNode* next = nullptr;
    };

    FlyGuiItem              exitItem_;
    FlyGuiItem              deleteItem_;
    FlyGuiItemCallback      exitCallback_         = nullptr;
    Context                 context_              = CONTEXT_NONE;
    BtHostList*             bluetoothHostList_    = nullptr;
    size_t                  itemCount_            = 0;
    size_t                  selectedIndex_        = 0;
    bool                    deleteMode_           = false;
    int32_t                 deleteHostIndex_      = -1;
    bool                    deleteHoldActive_     = false;
    bool                    deleteHoldShown_      = false;
    int32_t                 deleteHoldHostIndex_  = -1;
    uint32_t                deleteHoldStartMs_    = 0;
    GeneratedItemNode*      generatedHead_        = nullptr;
    GeneratedItemNode*      generatedTail_        = nullptr;
    ScrollViewClickCallback onBluetoothHost_      = nullptr;
    ScrollViewClickCallback onBluetoothPair_      = nullptr;
    ScrollViewClickCallback onWifiScanAndConnect_ = nullptr;
    ScrollViewClickCallback onWifiStation_        = nullptr;
    ScrollViewClickCallback onWifiAp_             = nullptr;
    ScrollViewClickCallback onCloudUpload_        = nullptr;
    ScrollViewClickCallback onNtpSync_            = nullptr;
    ScrollViewClickCallback onBtShowInfo_         = nullptr;
    ScrollViewClickCallback onWifiShowInfo_       = nullptr;
    ScrollViewClickCallback onFilePlayable_       = nullptr;
    ScrollViewClickCallback onFileListShowInfo_   = nullptr;
    static ScrollView*      activeDeleteView_;
};
