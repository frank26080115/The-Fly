#include "ScrollItem.h"

#include "IconLookup.h"
#include "sprites.h"
#include <string.h>

namespace
{
constexpr int16_t kScrollItemWidth  = 106;
constexpr int16_t kScrollItemHeight = 114;

void fallback_sprite(sprite_desc_t& sprite)
{
    sprite.data     = sprit_warning_100;
    sprite.width    = SPRIT_WARNING_100_WIDTH;
    sprite.height   = SPRIT_WARNING_100_HEIGHT;
    sprite.byte_cnt = SPRIT_WARNING_100_BYTES;
}
} // namespace

ScrollItem::ScrollItem() : FlyGuiItem(0, 0, kScrollItemWidth, kScrollItemHeight)
{
}

void ScrollItem::configure(ScrollItemKind kind, int32_t callbackValue, const char* label, uint8_t icon)
{
    kind_          = kind;
    callbackValue_ = callbackValue;
    icon_          = icon;

    strncpy(label_, label ? label : "", sizeof(label_) - 1);
    label_[sizeof(label_) - 1] = '\0';

    sprite_desc_t sprite = {};
    if (!IconLookup::getSprite(icon_, &sprite))
    {
        fallback_sprite(sprite);
    }

    setSprite(sprite.data, sprite.width, sprite.height, sprite.byte_cnt);
}

void ScrollItem::setScrollCallback(ScrollItemCallback callback, void* context)
{
    scrollCallback_  = callback;
    callbackContext_ = context;
    setTouchable(scrollCallback_ != nullptr);
}

bool ScrollItem::trigger(uint32_t pressDurationMs)
{
    if (!visible())
    {
        return false;
    }

    FlyGuiItem::trigger(pressDurationMs);

    if (scrollCallback_)
    {
        scrollCallback_(*this, callbackContext_, pressDurationMs);
    }

    return true;
}
