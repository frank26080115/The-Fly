#pragma once

#include <M5Unified.h>
#include <stddef.h>
#include <stdint.h>

namespace SpriteDraw
{

struct DrawResult
{
    bool     ok             = false;
    uint32_t decoded_width  = 0;
    uint32_t decoded_height = 0;
    uint32_t callbacks      = 0;
    uint32_t pixels         = 0;
    uint32_t elapsed_us     = 0;
};

using DrawCallback = void (*)();

DrawResult drawPng(M5GFX& display,
                   const uint8_t* sprite,
                   size_t sprite_bytes,
                   int32_t x,
                   int32_t y,
                   uint32_t width,
                   uint32_t height,
                   bool fast_mode,
                   DrawCallback callback = nullptr);

} // namespace SpriteDraw
