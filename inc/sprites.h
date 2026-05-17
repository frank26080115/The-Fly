#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(ARDUINO)
#include <pgmspace.h>
#endif

#ifndef PROGMEM
#define PROGMEM
#endif

typedef struct
{
    uint8_t* data; // set = to `(uint8_t*)sprit_*`
    uint16_t width;
    uint16_t height;
    uint32_t byte_cnt;
}
sprite_desc_t;


#define SPRIT_SPLASH_WIDTH 320u
#define SPRIT_SPLASH_HEIGHT 240u
#define SPRIT_SPLASH_BYTES 41821u
extern const uint8_t sprit_splash[];
