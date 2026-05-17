#include "IconLookup.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

namespace IconLookup
{
namespace
{

struct IconName
{
    const char* name;
    uint8_t     icon;
};

constexpr IconName kIconNames[] = {
    { "unknown", ICON_UNKNOWN },
    { "icon_unknown", ICON_UNKNOWN },
    { "phone", ICON_PHONE },
    { "icon_phone", ICON_PHONE },
    { "laptop", ICON_LAPTOP },
    { "computer", ICON_LAPTOP },
    { "icon_laptop", ICON_LAPTOP },
    { "tablet", ICON_TABLET },
    { "icon_tablet", ICON_TABLET },
    { "bluetooth", ICON_BLUETOOTH },
    { "bt", ICON_BLUETOOTH },
    { "icon_bluetooth", ICON_BLUETOOTH },
    { "wifi", ICON_WIFI },
    { "wi-fi", ICON_WIFI },
    { "icon_wifi", ICON_WIFI },
    { "home", ICON_HOME },
    { "icon_home", ICON_HOME },
    { "wifiap", ICON_WIFIAP },
    { "wifi_ap", ICON_WIFIAP },
    { "wifi-ap", ICON_WIFIAP },
    { "accesspoint", ICON_WIFIAP },
    { "access_point", ICON_WIFIAP },
    { "icon_wifiap", ICON_WIFIAP },
    { "www", ICON_WWW },
    { "web", ICON_WWW },
    { "icon_www", ICON_WWW },
    { "cloud", ICON_CLOUD },
    { "icon_cloud", ICON_CLOUD },
    { "ftp", ICON_FTP },
    { "icon_ftp", ICON_FTP },
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
    if (!sprite)
    {
        return;
    }

    sprite->data     = nullptr;
    sprite->width    = 0;
    sprite->height   = 0;
    sprite->byte_cnt = 0;
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
    case ICON_PHONE:
        return "phone";
    case ICON_LAPTOP:
        return "laptop";
    case ICON_TABLET:
        return "tablet";
    case ICON_BLUETOOTH:
        return "bluetooth";
    case ICON_WIFI:
        return "wifi";
    case ICON_HOME:
        return "home";
    case ICON_WIFIAP:
        return "wifiap";
    case ICON_WWW:
        return "www";
    case ICON_CLOUD:
        return "cloud";
    case ICON_FTP:
        return "ftp";
    case ICON_UNKNOWN:
    default:
        return "unknown";
    }
}

bool getSprite(uint8_t icon, sprite_desc_t* sprite)
{
    clear_sprite(sprite);

    switch (icon)
    {
    case ICON_PHONE:
        // TODO: assign sprite from non-existent sprit_icon_phone.
        break;
    case ICON_LAPTOP:
        // TODO: assign sprite from non-existent sprit_icon_laptop.
        break;
    case ICON_TABLET:
        // TODO: assign sprite from non-existent sprit_icon_tablet.
        break;
    case ICON_BLUETOOTH:
        // TODO: assign sprite from non-existent sprit_icon_bluetooth.
        break;
    case ICON_WIFI:
        // TODO: assign sprite from non-existent sprit_icon_wifi.
        break;
    case ICON_HOME:
        // TODO: assign sprite from non-existent sprit_icon_home.
        break;
    case ICON_WIFIAP:
        // TODO: assign sprite from non-existent sprit_icon_wifiap.
        break;
    case ICON_WWW:
        // TODO: assign sprite from non-existent sprit_icon_www.
        break;
    case ICON_CLOUD:
        // TODO: assign sprite from non-existent sprit_icon_cloud.
        break;
    case ICON_FTP:
        // TODO: assign sprite from non-existent sprit_icon_ftp.
        break;
    case ICON_UNKNOWN:
    default:
        // TODO: assign sprite from non-existent sprit_icon_unknown.
        break;
    }

    return false;
}

} // namespace IconLookup
