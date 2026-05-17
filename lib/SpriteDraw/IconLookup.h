#pragma once

#include <stdint.h>

#include "defs.h"
#include "sprites.h"

namespace IconLookup
{

uint8_t fromString(const char* value);
const char* toString(uint8_t icon);
bool    getSprite(uint8_t icon, sprite_desc_t* sprite);

} // namespace IconLookup
