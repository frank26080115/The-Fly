#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(ARDUINO)
#include <pgmspace.h>
#endif

#ifndef PROGMEM
#define PROGMEM
#endif

#define SPRIT_SPLASH_WIDTH 320u
#define SPRIT_SPLASH_HEIGHT 240u
#define SPRIT_SPLASH_BYTES 41821u
extern const uint8_t sprit_splash[];
