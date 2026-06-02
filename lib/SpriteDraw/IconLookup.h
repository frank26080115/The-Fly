#pragma once

#include "thefly_common.h"

#include <stddef.h>
#include <stdint.h>

#include "sprites.h"

struct sprite_desc_t
{
    const uint8_t* data             = nullptr;
    uint32_t       width            = 0;
    uint32_t       height           = 0;
    size_t         byte_cnt         = 0;
    const uint8_t* overlay_data     = nullptr;
    uint32_t       overlay_width    = 0;
    uint32_t       overlay_height   = 0;
    size_t         overlay_byte_cnt = 0;
    uint32_t       overlay_offset_x = 0;
    uint32_t       overlay_offset_y = 0;
};

namespace IconLookup
{

enum IconContext : uint8_t
{
    ICON_CONTEXT_BLUETOOTH,
    ICON_CONTEXT_WIFI,
    ICON_CONTEXT_CLOUD,
};

uint8_t     fromString(const char* value);
const char* toString(uint8_t icon);
bool        getSprite(uint8_t icon, IconContext usage_context, sprite_desc_t* sprite);

} // namespace IconLookup
