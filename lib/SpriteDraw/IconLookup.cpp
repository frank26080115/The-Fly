#include "IconLookup.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

namespace IconLookup
{
namespace
{

enum OverlayCorner : uint8_t
{
    OVERLAY_TOP_RIGHT,
    OVERLAY_BOTTOM_RIGHT,
};

struct IconName
{
    const char* name;
    uint8_t     icon;
};

struct SpriteRef
{
    const uint8_t* data;
    uint32_t       width;
    uint32_t       height;
    size_t         byte_cnt;
};

constexpr IconName kIconNames[] = {
    { "unknown", ICON_UNKNOWN },
    { "icon_unknown", ICON_UNKNOWN },
    { "smartphone", ICON_SMARTPHONE },
    { "phone", ICON_SMARTPHONE },
    { "mobile", ICON_SMARTPHONE },
    { "icon_smartphone", ICON_SMARTPHONE },
    { "icon_phone", ICON_SMARTPHONE },
    { "phone_ap", ICON_SMARTPHONE },
    { "phone-ap", ICON_SMARTPHONE },
    { "smartphone_ap", ICON_SMARTPHONE },
    { "smartphone-ap", ICON_SMARTPHONE },
    { "icon_phone_ap", ICON_SMARTPHONE },
    { "laptop", ICON_LAPTOP },
    { "computer", ICON_LAPTOP },
    { "icon_laptop", ICON_LAPTOP },
    { "tablet", ICON_TABLET },
    { "icon_tablet", ICON_TABLET },
    { "home", ICON_HOME },
    { "house", ICON_HOME },
    { "icon_home", ICON_HOME },
    { "cat", ICON_CAT },
    { "icon_cat", ICON_CAT },
    { "dog", ICON_DOG },
    { "icon_dog", ICON_DOG },
    { "bird", ICON_BIRD },
    { "icon_bird", ICON_BIRD },
    { "circle", ICON_CIRCLE },
    { "icon_circle", ICON_CIRCLE },
    { "square", ICON_SQUARE },
    { "icon_square", ICON_SQUARE },
    { "triangle", ICON_TRIANGLE },
    { "icon_triangle", ICON_TRIANGLE },
};

bool equal_ignore_case(const char* lhs, const char* rhs)
{
    if (!lhs || !rhs)
    {
        return false;
    }

    while (*lhs && *rhs)
    {
        const int a = tolower(static_cast<unsigned char>(*lhs));
        const int b = tolower(static_cast<unsigned char>(*rhs));
        if (a != b)
        {
            return false;
        }
        ++lhs;
        ++rhs;
    }

    return *lhs == '\0' && *rhs == '\0';
}

void clear_sprite(sprite_desc_t* sprite)
{
    if (sprite)
    {
        *sprite = {};
    }
}

void assign_sprite(sprite_desc_t* sprite, SpriteRef ref)
{
    if (!sprite)
    {
        return;
    }

    sprite->data     = ref.data;
    sprite->width    = ref.width;
    sprite->height   = ref.height;
    sprite->byte_cnt = ref.byte_cnt;
}

void assign_overlay(sprite_desc_t* sprite, SpriteRef ref, OverlayCorner corner)
{
    if (!sprite)
    {
        return;
    }

    sprite->overlay_data     = ref.data;
    sprite->overlay_width    = ref.width;
    sprite->overlay_height   = ref.height;
    sprite->overlay_byte_cnt = ref.byte_cnt;

    if (sprite->width > ref.width)
    {
        sprite->overlay_offset_x = sprite->width - ref.width;
    }

    if (corner == OVERLAY_BOTTOM_RIGHT && sprite->height > ref.height)
    {
        sprite->overlay_offset_y = sprite->height - ref.height;
    }
}

SpriteRef cloud_upload()
{
    return { sprit_cloudupload_100, SPRIT_CLOUDUPLOAD_100_WIDTH, SPRIT_CLOUDUPLOAD_100_HEIGHT, SPRIT_CLOUDUPLOAD_100_BYTES };
}

SpriteRef generic_bt()
{
    return { sprit_genericdevice_bt_100, SPRIT_GENERICDEVICE_BT_100_WIDTH, SPRIT_GENERICDEVICE_BT_100_HEIGHT, SPRIT_GENERICDEVICE_BT_100_BYTES };
}

SpriteRef generic_wifi_router()
{
    return { sprit_genericwifirouter_100, SPRIT_GENERICWIFIROUTER_100_WIDTH, SPRIT_GENERICWIFIROUTER_100_HEIGHT, SPRIT_GENERICWIFIROUTER_100_BYTES };
}

SpriteRef home()
{
    return { sprit_home_100, SPRIT_HOME_100_WIDTH, SPRIT_HOME_100_HEIGHT, SPRIT_HOME_100_BYTES };
}

SpriteRef home_wifi()
{
    return { sprit_home_wifi_100, SPRIT_HOME_WIFI_100_WIDTH, SPRIT_HOME_WIFI_100_HEIGHT, SPRIT_HOME_WIFI_100_BYTES };
}

SpriteRef laptop()
{
    return { sprit_laptop_100, SPRIT_LAPTOP_100_WIDTH, SPRIT_LAPTOP_100_HEIGHT, SPRIT_LAPTOP_100_BYTES };
}

SpriteRef laptop_bt()
{
    return { sprit_laptop_bt_100, SPRIT_LAPTOP_BT_100_WIDTH, SPRIT_LAPTOP_BT_100_HEIGHT, SPRIT_LAPTOP_BT_100_BYTES };
}

SpriteRef overlay_bird()
{
    return { sprit_overlay_bird_50, SPRIT_OVERLAY_BIRD_50_WIDTH, SPRIT_OVERLAY_BIRD_50_HEIGHT, SPRIT_OVERLAY_BIRD_50_BYTES };
}

SpriteRef overlay_bt()
{
    return { sprit_overlay_bt_50, SPRIT_OVERLAY_BT_50_WIDTH, SPRIT_OVERLAY_BT_50_HEIGHT, SPRIT_OVERLAY_BT_50_BYTES };
}

SpriteRef overlay_cat()
{
    return { sprit_overlay_cat_50, SPRIT_OVERLAY_CAT_50_WIDTH, SPRIT_OVERLAY_CAT_50_HEIGHT, SPRIT_OVERLAY_CAT_50_BYTES };
}

SpriteRef overlay_circle()
{
    return { sprit_overlay_circle_40, SPRIT_OVERLAY_CIRCLE_40_WIDTH, SPRIT_OVERLAY_CIRCLE_40_HEIGHT, SPRIT_OVERLAY_CIRCLE_40_BYTES };
}

SpriteRef overlay_cloud()
{
    return { sprit_overlay_cloud_50, SPRIT_OVERLAY_CLOUD_50_WIDTH, SPRIT_OVERLAY_CLOUD_50_HEIGHT, SPRIT_OVERLAY_CLOUD_50_BYTES };
}

SpriteRef overlay_dog()
{
    return { sprit_overlay_dog_50, SPRIT_OVERLAY_DOG_50_WIDTH, SPRIT_OVERLAY_DOG_50_HEIGHT, SPRIT_OVERLAY_DOG_50_BYTES };
}

SpriteRef overlay_home()
{
    return { sprit_overlay_home_50, SPRIT_OVERLAY_HOME_50_WIDTH, SPRIT_OVERLAY_HOME_50_HEIGHT, SPRIT_OVERLAY_HOME_50_BYTES };
}

SpriteRef overlay_square()
{
    return { sprit_overlay_square_40, SPRIT_OVERLAY_SQUARE_40_WIDTH, SPRIT_OVERLAY_SQUARE_40_HEIGHT, SPRIT_OVERLAY_SQUARE_40_BYTES };
}

SpriteRef overlay_triangle()
{
    return { sprit_overlay_triangle_40, SPRIT_OVERLAY_TRIANGLE_40_WIDTH, SPRIT_OVERLAY_TRIANGLE_40_HEIGHT, SPRIT_OVERLAY_TRIANGLE_40_BYTES };
}

SpriteRef overlay_wifi()
{
    return { sprit_overlay_wifi_50, SPRIT_OVERLAY_WIFI_50_WIDTH, SPRIT_OVERLAY_WIFI_50_HEIGHT, SPRIT_OVERLAY_WIFI_50_BYTES };
}

SpriteRef smartphone()
{
    return { sprit_smartphone_100, SPRIT_SMARTPHONE_100_WIDTH, SPRIT_SMARTPHONE_100_HEIGHT, SPRIT_SMARTPHONE_100_BYTES };
}

SpriteRef smartphone_ap()
{
    return { sprit_smartphone_ap_100, SPRIT_SMARTPHONE_AP_100_WIDTH, SPRIT_SMARTPHONE_AP_100_HEIGHT, SPRIT_SMARTPHONE_AP_100_BYTES };
}

SpriteRef smartphone_bt()
{
    return { sprit_smartphone_bt_100, SPRIT_SMARTPHONE_BT_100_WIDTH, SPRIT_SMARTPHONE_BT_100_HEIGHT, SPRIT_SMARTPHONE_BT_100_BYTES };
}

SpriteRef tablet()
{
    return { sprit_tablet_100, SPRIT_TABLET_100_WIDTH, SPRIT_TABLET_100_HEIGHT, SPRIT_TABLET_100_BYTES };
}

SpriteRef tablet_bt()
{
    return { sprit_tablet_bt_100, SPRIT_TABLET_BT_100_WIDTH, SPRIT_TABLET_BT_100_HEIGHT, SPRIT_TABLET_BT_100_BYTES };
}

SpriteRef unknown_bt()
{
    return { sprit_unknown_bt_100, SPRIT_UNKNOWN_BT_100_WIDTH, SPRIT_UNKNOWN_BT_100_HEIGHT, SPRIT_UNKNOWN_BT_100_BYTES };
}

void assign_generic_overlay(sprite_desc_t* sprite, SpriteRef base, uint8_t icon)
{
    assign_sprite(sprite, base);

    switch (icon)
    {
    case ICON_CAT:
        assign_overlay(sprite, overlay_cat(), OVERLAY_BOTTOM_RIGHT);
        break;
    case ICON_DOG:
        assign_overlay(sprite, overlay_dog(), OVERLAY_BOTTOM_RIGHT);
        break;
    case ICON_BIRD:
        assign_overlay(sprite, overlay_bird(), OVERLAY_BOTTOM_RIGHT);
        break;
    case ICON_CIRCLE:
        assign_overlay(sprite, overlay_square(), OVERLAY_BOTTOM_RIGHT);
        break;
    case ICON_SQUARE:
        assign_overlay(sprite, overlay_circle(), OVERLAY_BOTTOM_RIGHT);
        break;
    case ICON_TRIANGLE:
        assign_overlay(sprite, overlay_triangle(), OVERLAY_BOTTOM_RIGHT);
        break;
    default:
        break;
    }
}

void assign_bluetooth_sprite(uint8_t icon, sprite_desc_t* sprite)
{
    switch (icon)
    {
    case ICON_SMARTPHONE:
        assign_sprite(sprite, smartphone_bt());
        break;
    case ICON_LAPTOP:
        assign_sprite(sprite, laptop_bt());
        break;
    case ICON_TABLET:
        assign_sprite(sprite, tablet_bt());
        break;
    case ICON_HOME:
        assign_sprite(sprite, home());
        assign_overlay(sprite, overlay_bt(), OVERLAY_TOP_RIGHT);
        break;
    case ICON_CAT:
    case ICON_DOG:
    case ICON_BIRD:
    case ICON_CIRCLE:
    case ICON_SQUARE:
    case ICON_TRIANGLE:
        assign_generic_overlay(sprite, generic_bt(), icon);
        break;
    case ICON_UNKNOWN:
    default:
        assign_sprite(sprite, unknown_bt());
        break;
    }
}

void assign_wifi_sprite(uint8_t icon, sprite_desc_t* sprite)
{
    switch (icon)
    {
    case ICON_SMARTPHONE:
        assign_sprite(sprite, smartphone_ap());
        break;
    case ICON_LAPTOP:
        assign_sprite(sprite, laptop());
        assign_overlay(sprite, overlay_wifi(), OVERLAY_TOP_RIGHT);
        break;
    case ICON_TABLET:
        assign_sprite(sprite, tablet());
        assign_overlay(sprite, overlay_wifi(), OVERLAY_TOP_RIGHT);
        break;
    case ICON_HOME:
        assign_sprite(sprite, home_wifi());
        break;
    case ICON_CAT:
    case ICON_DOG:
    case ICON_BIRD:
    case ICON_CIRCLE:
    case ICON_SQUARE:
    case ICON_TRIANGLE:
        assign_generic_overlay(sprite, generic_wifi_router(), icon);
        break;
    case ICON_UNKNOWN:
    default:
        assign_sprite(sprite, generic_wifi_router());
        break;
    }
}

void assign_cloud_sprite(uint8_t icon, sprite_desc_t* sprite)
{
    switch (icon)
    {
    case ICON_SMARTPHONE:
        assign_sprite(sprite, smartphone());
        assign_overlay(sprite, overlay_cloud(), OVERLAY_TOP_RIGHT);
        break;
    case ICON_LAPTOP:
        assign_sprite(sprite, laptop());
        assign_overlay(sprite, overlay_cloud(), OVERLAY_TOP_RIGHT);
        break;
    case ICON_TABLET:
        assign_sprite(sprite, tablet());
        assign_overlay(sprite, overlay_cloud(), OVERLAY_TOP_RIGHT);
        break;
    case ICON_HOME:
        assign_sprite(sprite, cloud_upload());
        assign_overlay(sprite, overlay_home(), OVERLAY_BOTTOM_RIGHT);
        break;
    case ICON_CAT:
    case ICON_DOG:
    case ICON_BIRD:
    case ICON_CIRCLE:
    case ICON_SQUARE:
    case ICON_TRIANGLE:
        assign_generic_overlay(sprite, cloud_upload(), icon);
        break;
    case ICON_UNKNOWN:
    default:
        assign_sprite(sprite, cloud_upload());
        break;
    }
}

} // namespace

uint8_t fromString(const char* value)
{
    if (!value)
    {
        return ICON_UNKNOWN;
    }

    for (const IconName& item : kIconNames)
    {
        if (equal_ignore_case(value, item.name))
        {
            return item.icon;
        }
    }

    return ICON_UNKNOWN;
}

const char* toString(uint8_t icon)
{
    switch (icon)
    {
    case ICON_SMARTPHONE:
        return "smartphone";
    case ICON_LAPTOP:
        return "laptop";
    case ICON_TABLET:
        return "tablet";
    case ICON_HOME:
        return "home";
    case ICON_CAT:
        return "cat";
    case ICON_DOG:
        return "dog";
    case ICON_BIRD:
        return "bird";
    case ICON_CIRCLE:
        return "circle";
    case ICON_SQUARE:
        return "square";
    case ICON_TRIANGLE:
        return "triangle";
    case ICON_UNKNOWN:
    default:
        return "unknown";
    }
}

bool getSprite(uint8_t icon, IconContext usage_context, sprite_desc_t* sprite)
{
    clear_sprite(sprite);
    if (!sprite)
    {
        return false;
    }

    switch (usage_context)
    {
    case ICON_CONTEXT_BLUETOOTH:
        assign_bluetooth_sprite(icon, sprite);
        break;
    case ICON_CONTEXT_WIFI:
        assign_wifi_sprite(icon, sprite);
        break;
    case ICON_CONTEXT_CLOUD:
        assign_cloud_sprite(icon, sprite);
        break;
    default:
        return false;
    }

    return sprite->data && sprite->byte_cnt > 0 && sprite->width > 0 && sprite->height > 0;
}

} // namespace IconLookup
