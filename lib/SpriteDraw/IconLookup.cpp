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
    { "phone_ap", ICON_PHONE_AP },
    { "phone-ap", ICON_PHONE_AP },
    { "smartphone_ap", ICON_PHONE_AP },
    { "smartphone-ap", ICON_PHONE_AP },
    { "icon_phone_ap", ICON_PHONE_AP },
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
    { "wifi_search", ICON_WIFI_SEARCH },
    { "wifisearch", ICON_WIFI_SEARCH },
    { "wifi-search", ICON_WIFI_SEARCH },
    { "scan_wifi", ICON_WIFI_SEARCH },
    { "wifi_scan", ICON_WIFI_SEARCH },
    { "icon_wifi_search", ICON_WIFI_SEARCH },
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
    { "ntp", ICON_NTP },
    { "time", ICON_NTP },
    { "sync_time", ICON_NTP },
    { "time_sync", ICON_NTP },
    { "icon_ntp", ICON_NTP },
    { "info", ICON_INFO },
    { "information", ICON_INFO },
    { "icon_info", ICON_INFO },
    { "btpairing", ICON_BTPAIRING },
    { "bt_pairing", ICON_BTPAIRING },
    { "bluetooth_pairing", ICON_BTPAIRING },
    { "pairing", ICON_BTPAIRING },
    { "icon_btpairing", ICON_BTPAIRING },
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

void assign_sprite(sprite_desc_t* sprite, const uint8_t* data, uint32_t width, uint32_t height, size_t byte_cnt)
{
    if (!sprite)
    {
        return;
    }

    sprite->data     = data;
    sprite->width    = width;
    sprite->height   = height;
    sprite->byte_cnt = byte_cnt;
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
    case ICON_PHONE_AP:
        return "phone_ap";
    case ICON_LAPTOP:
        return "laptop";
    case ICON_TABLET:
        return "tablet";
    case ICON_BLUETOOTH:
        return "bluetooth";
    case ICON_WIFI:
        return "wifi";
    case ICON_WIFI_SEARCH:
        return "wifi_search";
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
    case ICON_NTP:
        return "ntp";
    case ICON_INFO:
        return "info";
    case ICON_BTPAIRING:
        return "btpairing";
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
        assign_sprite(sprite, sprit_smartphone_100, SPRIT_SMARTPHONE_100_WIDTH, SPRIT_SMARTPHONE_100_HEIGHT, SPRIT_SMARTPHONE_100_BYTES);
        return true;
    case ICON_PHONE_AP:
        assign_sprite(sprite, sprit_smartphone_ap_100, SPRIT_SMARTPHONE_AP_100_WIDTH, SPRIT_SMARTPHONE_AP_100_HEIGHT, SPRIT_SMARTPHONE_AP_100_BYTES);
        return true;
    case ICON_LAPTOP:
        assign_sprite(sprite, sprit_laptop_100, SPRIT_LAPTOP_100_WIDTH, SPRIT_LAPTOP_100_HEIGHT, SPRIT_LAPTOP_100_BYTES);
        return true;
    case ICON_TABLET:
        assign_sprite(sprite, sprit_tablet_100, SPRIT_TABLET_100_WIDTH, SPRIT_TABLET_100_HEIGHT, SPRIT_TABLET_100_BYTES);
        return true;
    case ICON_BLUETOOTH:
        assign_sprite(sprite, sprit_bluetooth_100, SPRIT_BLUETOOTH_100_WIDTH, SPRIT_BLUETOOTH_100_HEIGHT, SPRIT_BLUETOOTH_100_BYTES);
        return true;
    case ICON_WIFI:
        assign_sprite(sprite, sprit_wifi_100, SPRIT_WIFI_100_WIDTH, SPRIT_WIFI_100_HEIGHT, SPRIT_WIFI_100_BYTES);
        return true;
    case ICON_WIFI_SEARCH:
        assign_sprite(sprite, sprit_wifisearch_100, SPRIT_WIFISEARCH_100_WIDTH, SPRIT_WIFISEARCH_100_HEIGHT, SPRIT_WIFISEARCH_100_BYTES);
        return true;
    case ICON_HOME:
        assign_sprite(sprite, sprit_home_100, SPRIT_HOME_100_WIDTH, SPRIT_HOME_100_HEIGHT, SPRIT_HOME_100_BYTES);
        return true;
    case ICON_WIFIAP:
        assign_sprite(sprite, sprit_home_wifi_100, SPRIT_HOME_WIFI_100_WIDTH, SPRIT_HOME_WIFI_100_HEIGHT, SPRIT_HOME_WIFI_100_BYTES);
        return true;
    case ICON_WWW:
        break;
    case ICON_CLOUD:
        assign_sprite(sprite, sprit_cloudupload_100, SPRIT_CLOUDUPLOAD_100_WIDTH, SPRIT_CLOUDUPLOAD_100_HEIGHT, SPRIT_CLOUDUPLOAD_100_BYTES);
        return true;
    case ICON_FTP:
        break;
    case ICON_NTP:
        assign_sprite(sprite, sprit_ntpsync_100, SPRIT_NTPSYNC_100_WIDTH, SPRIT_NTPSYNC_100_HEIGHT, SPRIT_NTPSYNC_100_BYTES);
        return true;
    case ICON_INFO:
        assign_sprite(sprite, sprit_info_100, SPRIT_INFO_100_WIDTH, SPRIT_INFO_100_HEIGHT, SPRIT_INFO_100_BYTES);
        return true;
    case ICON_BTPAIRING:
        assign_sprite(sprite, sprit_btpairing_100, SPRIT_BTPAIRING_100_WIDTH, SPRIT_BTPAIRING_100_HEIGHT, SPRIT_BTPAIRING_100_BYTES);
        return true;
    case ICON_UNKNOWN:
    default:
        break;
    }

    return false;
}

} // namespace IconLookup
