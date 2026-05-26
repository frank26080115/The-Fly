#pragma once

#include "../../SpriteDraw/IconLookup.h"
#include "../../FlyGui/FlyGui.h"
#include <stdint.h>

enum ScrollItemKind : uint8_t
{
    SCROLL_ITEM_BLUETOOTH_HOST,
    SCROLL_ITEM_BLUETOOTH_PAIRING,
    SCROLL_ITEM_BLUETOOTH_SHOW_SELF_INFO,
    SCROLL_ITEM_WIFI_SCAN_AND_CONNECT,
    SCROLL_ITEM_WIFI_STATION,
    SCROLL_ITEM_WIFI_AP,
    SCROLL_ITEM_CLOUD_ENDPOINT,
    SCROLL_ITEM_NTP_SYNC,
    SCROLL_ITEM_WIFI_SHOW_SELF_INFO,
};

enum ScrollItemTask : int32_t
{
    SCROLL_TASK_BLUETOOTH_PAIRING        = -1,
    SCROLL_TASK_BLUETOOTH_SHOW_SELF_INFO = -2,
    SCROLL_TASK_NTP_SYNC                 = -3,
    SCROLL_TASK_WIFI_SHOW_SELF_INFO      = -4,
    SCROLL_TASK_WIFI_SCAN_AND_CONNECT    = -5,
    SCROLL_TASK_WIFI_GENERATED_AP        = -6,
};

class ScrollItem;
using ScrollItemCallback = void (*)(ScrollItem& item, void* context, uint32_t pressDurationMs);

class ScrollItem : public FlyGuiItem
{
public:
    static constexpr size_t kLabelCapacity = 256;

    ScrollItem();

    void configure(ScrollItemKind kind, int32_t callbackValue, const char* label, uint8_t icon, IconLookup::IconContext iconContext);
    void configureSprite(ScrollItemKind kind, int32_t callbackValue, const char* label, const sprite_desc_t& sprite);
    void setScrollCallback(ScrollItemCallback callback, void* context);

    ScrollItemKind kind() const
    {
        return kind_;
    }

    int32_t callbackValue() const
    {
        return callbackValue_;
    }

    uint8_t icon() const
    {
        return icon_;
    }

    const char* label() const
    {
        return label_;
    }

    bool trigger(uint32_t pressDurationMs = 0) override;

private:
    ScrollItemKind     kind_          = SCROLL_ITEM_BLUETOOTH_HOST;
    int32_t            callbackValue_ = 0;
    uint8_t            icon_          = 0;
    ScrollItemCallback scrollCallback_ = nullptr;
    void*              callbackContext_ = nullptr;
    char               label_[kLabelCapacity] = {};
};
