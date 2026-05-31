#include "SpriteDraw.h"

#include "Display.h"
#include "thefly_common.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include "dbg_log.h"
#include <lgfx/utility/lgfx_pngle.h>
#include <pgmspace.h>

namespace SpriteDraw
{
namespace
{

constexpr size_t kMaxRunChunkPixels = 128;
constexpr const char* TAG = "SpriteDraw";
constexpr uint8_t kTransparentAlphaThreshold = 255 / 4;

struct PngDecodeContext
{
    const uint8_t* data       = nullptr;
    size_t         data_size  = 0;
    size_t         read_index = 0;
    int32_t        origin_x   = 0;
    int32_t        origin_y   = 0;
    uint32_t       callbacks  = 0;
    uint32_t       pixels     = 0;
    bool           fast_mode  = false;
    uint8_t        brightness = PNG_BRTNESS_100;
    DrawCallback   callback   = nullptr;
};

lgfx::rgb565_t argb_to_rgb565(const uint8_t* argb, uint8_t brightness);

bool dither_pixel_enabled(int32_t x, int32_t y)
{
    return (x & 1) == (y & 1);
}

lgfx::rgb565_t black_pixel()
{
    return lgfx::rgb565_t(0, 0, 0);
}

bool considered_transparent_pixel(const uint8_t* argb)
{
    return argb[0] <= kTransparentAlphaThreshold;
}

void draw_png_run(void* user_data, uint32_t x, uint32_t y, uint_fast8_t div_x, size_t length, const uint8_t* argb)
{
    auto* context = static_cast<PngDecodeContext*>(user_data);
    if (context == nullptr || argb == nullptr || length == 0)
    {
        return;
    }

    int32_t dst_x = context->origin_x + static_cast<int32_t>(x);
    int32_t dst_y = context->origin_y + static_cast<int32_t>(y);
    const bool dither = (context->brightness & PNG_DITHER_FLAG) != 0;

    ++context->callbacks;
    context->pixels += static_cast<uint32_t>(length);

    if (context->callback)
    {
        context->callback();
    }
    else
    {
        if (!context->fast_mode)
        {
            taskYIELD();
        }
    }

    if (dst_y < 0 || dst_y >= thefly_display.height())
    {
        return;
    }

    if (div_x != 1)
    {
        if (!context->fast_mode)
        {
            thefly_display.startWrite();
        }

        for (size_t index = 0; index < length; ++index)
        {
            const int32_t pixel_x = dst_x + static_cast<int32_t>(index * div_x);
            if (!considered_transparent_pixel(argb) && pixel_x >= 0 && pixel_x < thefly_display.width())
            {
                const lgfx::rgb565_t color = !dither || dither_pixel_enabled(pixel_x, dst_y)
                                               ? argb_to_rgb565(argb, context->brightness)
                                               : black_pixel();
                thefly_display.drawPixel(pixel_x, dst_y, color);
            }
            argb += 4;
        }

        if (!context->fast_mode)
        {
            thefly_display.endWrite();
        }

        return;
    }

    if (dst_x < 0)
    {
        const size_t skip = static_cast<size_t>(-dst_x);
        if (skip >= length)
        {
            return;
        }
        argb += skip * 4;
        length -= skip;
        dst_x = 0;
    }

    if (dst_x >= thefly_display.width())
    {
        return;
    }

    const size_t max_visible = static_cast<size_t>(thefly_display.width() - dst_x);
    if (length > max_visible)
    {
        length = max_visible;
    }

    lgfx::rgb565_t line[kMaxRunChunkPixels];
    if (!context->fast_mode)
    {
        thefly_display.startWrite();
    }

    while (length > 0)
    {
        const size_t chunk = length < kMaxRunChunkPixels ? length : kMaxRunChunkPixels;
        size_t       index = 0;
        while (index < chunk)
        {
            while (index < chunk && considered_transparent_pixel(argb + index * 4))
            {
                ++index;
            }

            if (index >= chunk)
            {
                break;
            }

            const size_t span_start = index;
            size_t       span_len   = 0;
            while (index < chunk && !considered_transparent_pixel(argb + index * 4))
            {
                const int32_t pixel_x = dst_x + static_cast<int32_t>(index);
                line[span_len++] = !dither || dither_pixel_enabled(pixel_x, dst_y)
                                       ? argb_to_rgb565(argb + index * 4, context->brightness)
                                       : black_pixel();
                ++index;
            }

            thefly_display.setAddrWindow(dst_x + static_cast<int32_t>(span_start), dst_y, static_cast<int32_t>(span_len), 1);
            thefly_display.writePixels(line, static_cast<int32_t>(span_len));
        }

        dst_x += static_cast<int32_t>(chunk);
        argb += chunk * 4;
        length -= chunk;
    }

    if (!context->fast_mode)
    {
        thefly_display.endWrite();
    }
}

uint32_t read_png_bytes(void* user_data, uint8_t* buffer, uint32_t length)
{
    auto* context = static_cast<PngDecodeContext*>(user_data);
    if (context == nullptr || context->data == nullptr || context->read_index > context->data_size)
    {
        return 0;
    }

    const size_t remaining = context->data_size - context->read_index;
    const size_t count     = length < remaining ? length : remaining;

    if (buffer != nullptr)
    {
        for (size_t index = 0; index < count; ++index)
        {
            buffer[index] = pgm_read_byte(context->data + context->read_index + index);
        }
    }

    context->read_index += count;
    return static_cast<uint32_t>(count);
}

uint16_t apply_brightness(uint16_t value, uint8_t brightness)
{
    switch (brightness & ~PNG_DITHER_FLAG)
    {
    case PNG_BRTNESS_75:
        return static_cast<uint16_t>((value * 3U + 2U) / 4U);
    case PNG_BRTNESS_50:
        return static_cast<uint16_t>(value >> 1);
    case PNG_BRTNESS_25:
        return static_cast<uint16_t>(value >> 2);
    case PNG_BRTNESS_12:
        return static_cast<uint16_t>(value >> 3);
    case PNG_BRTNESS_6:
        return static_cast<uint16_t>(value >> 4);
    case PNG_BRTNESS_100:
    default:
        return value;
    }
}

lgfx::rgb565_t argb_to_rgb565(const uint8_t* argb, uint8_t brightness)
{
    const uint16_t alpha = argb[0];
    uint16_t       red   = argb[1];
    uint16_t       green = argb[2];
    uint16_t       blue  = argb[3];

    if (alpha != 255)
    {
        red   = static_cast<uint16_t>((red * alpha + 127) / 255);
        green = static_cast<uint16_t>((green * alpha + 127) / 255);
        blue  = static_cast<uint16_t>((blue * alpha + 127) / 255);
    }

    red   = apply_brightness(red, brightness);
    green = apply_brightness(green, brightness);
    blue  = apply_brightness(blue, brightness);

    return lgfx::rgb565_t(static_cast<uint8_t>(red), static_cast<uint8_t>(green), static_cast<uint8_t>(blue));
}

} // namespace

DrawResult drawPng(const uint8_t* sprite,
                   size_t sprite_bytes,
                   int32_t x,
                   int32_t y,
                   uint32_t width,
                   uint32_t height,
                   bool fast_mode,
                   uint8_t brightness,
                   DrawCallback callback)
{
    DrawResult result;

    if (sprite == nullptr || sprite_bytes == 0 || width == 0 || height == 0)
    {
        result.failure_stage = DRAW_FAILURE_INVALID_ARGUMENT;
        return result;
    }

    pngle_t* pngle = lgfx_pngle_new();
    if (pngle == nullptr)
    {
        result.failure_stage = DRAW_FAILURE_ALLOC;
        return result;
    }

    PngDecodeContext context;
    context.data      = sprite;
    context.data_size = sprite_bytes;
    context.origin_x  = x;
    context.origin_y  = y;
    context.fast_mode = fast_mode;
    context.brightness = brightness;
    context.callback  = callback;

    if (lgfx_pngle_prepare(pngle, read_png_bytes, &context) < 0)
    {
        result.callbacks     = context.callbacks;
        result.pixels        = context.pixels;
        result.read_bytes    = static_cast<uint32_t>(context.read_index);
        result.failure_stage = DRAW_FAILURE_PREPARE;
        lgfx_pngle_destroy(pngle);
        return result;
    }

    result.decoded_width  = lgfx_pngle_get_width(pngle);
    result.decoded_height = lgfx_pngle_get_height(pngle);

    if (result.decoded_width != width || result.decoded_height != height)
    {
        result.read_bytes    = static_cast<uint32_t>(context.read_index);
        result.failure_stage = DRAW_FAILURE_SIZE_MISMATCH;
        lgfx_pngle_destroy(pngle);
        return result;
    }

    const uint32_t started_us = micros();
    if (fast_mode)
    {
        thefly_display.startWrite();
    }
    const int decode_result = lgfx_pngle_decomp(pngle, draw_png_run);
    if (fast_mode)
    {
        thefly_display.endWrite();
    }
    result.elapsed_us = micros() - started_us;

    result.ok            = decode_result >= 0;
    result.callbacks     = context.callbacks;
    result.pixels        = context.pixels;
    result.read_bytes    = static_cast<uint32_t>(context.read_index);
    result.decode_result = decode_result;
    if (!result.ok)
    {
        result.failure_stage = DRAW_FAILURE_DECODE;
    }

    lgfx_pngle_destroy(pngle);
    return result;
}

} // namespace SpriteDraw
