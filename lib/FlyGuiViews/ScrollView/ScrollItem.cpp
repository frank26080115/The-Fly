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
    sprite.data     = sprit_btn_questionmark;
    sprite.width    = SPRIT_BTN_QUESTIONMARK_WIDTH;
    sprite.height   = SPRIT_BTN_QUESTIONMARK_HEIGHT;
    sprite.byte_cnt = SPRIT_BTN_QUESTIONMARK_BYTES;
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

bool ScrollItem::trigger()
{
    if (!visible())
    {
        return false;
    }

    if (scrollCallback_)
    {
        scrollCallback_(*this, callbackContext_);
    }

    return true;
}
