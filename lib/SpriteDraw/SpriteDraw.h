#pragma once

#include <M5Unified.h>
#include <stddef.h>
#include <stdint.h>

namespace SpriteDraw
{

enum DrawFailureStage : uint8_t
{
    DRAW_FAILURE_NONE = 0,
    DRAW_FAILURE_INVALID_ARGUMENT,
    DRAW_FAILURE_ALLOC,
    DRAW_FAILURE_PREPARE,
    DRAW_FAILURE_SIZE_MISMATCH,
    DRAW_FAILURE_DECODE,
};

struct DrawResult
{
    bool             ok             = false;
    uint32_t         decoded_width  = 0;
    uint32_t         decoded_height = 0;
    uint32_t         callbacks      = 0;
    uint32_t         pixels         = 0;
    uint32_t         elapsed_us     = 0;
    uint32_t         read_bytes     = 0;
    int32_t          decode_result  = 0;
    DrawFailureStage failure_stage  = DRAW_FAILURE_NONE;
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
