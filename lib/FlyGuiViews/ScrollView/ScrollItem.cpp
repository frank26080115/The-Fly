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
    sprite = { sprite_warning_100, SPRITE_WARNING_100_WIDTH, SPRITE_WARNING_100_HEIGHT, SPRITE_WARNING_100_BYTES };
}
} // namespace

ScrollItem::ScrollItem() : FlyGuiItem(0, 0, kScrollItemWidth, kScrollItemHeight)
{
}

void ScrollItem::configure(ScrollItemKind kind, int32_t callbackValue, const char* label, uint8_t icon, IconLookup::IconContext iconContext)
{
    kind_          = kind;
    callbackValue_ = callbackValue;
    icon_          = icon;

    strncpy(label_, label ? label : "", sizeof(label_) - 1);
    label_[sizeof(label_) - 1] = '\0';

    sprite_desc_t sprite = {};
    if (!IconLookup::getSprite(icon_, iconContext, &sprite))
    {
        fallback_sprite(sprite);
    }

    setSprite(sprite);
}

void ScrollItem::configureSprite(ScrollItemKind kind, int32_t callbackValue, const char* label, const sprite_desc_t& sprite)
{
    kind_          = kind;
    callbackValue_ = callbackValue;
    icon_          = ICON_UNKNOWN;

    strncpy(label_, label ? label : "", sizeof(label_) - 1);
    label_[sizeof(label_) - 1] = '\0';

    setSprite(sprite);
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
