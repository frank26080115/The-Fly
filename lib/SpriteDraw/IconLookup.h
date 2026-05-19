#pragma once

#include <stddef.h>
#include <stdint.h>

#include "defs.h"
#include "sprites.h"

typedef struct
{
    const uint8_t* data;
    uint32_t       width;
    uint32_t       height;
    size_t         byte_cnt;
}
sprite_desc_t;

namespace IconLookup
{

uint8_t fromString(const char* value);
const char* toString(uint8_t icon);
bool    getSprite(uint8_t icon, sprite_desc_t* sprite);

} // namespace IconLookup
