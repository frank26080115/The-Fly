#include "GuiDisplay.h"

#ifdef TEST_BUILD_SCREENSHOT

#include "ClockAgent.h"
#include "MicroSdCard.h"
#include "dbg_log.h"

#include <SdFat.h>
#include <new>
#include <stdio.h>
#include <string.h>

namespace
{

constexpr const char* TAG                 = "GuiDisplay";
constexpr const char* kScreenshotDir      = "/screenshots";
constexpr int32_t     kBmpHeaderBytes     = 54;
constexpr uint16_t    kBmpBitsPerPixel    = 24;
constexpr uint32_t    kBmpInfoHeaderBytes = 40;
constexpr uint16_t    kBmpPlanes          = 1;
constexpr uint32_t    kBmpPelsPerMeter    = 2835;

static void writeLe16(uint8_t* dst, uint16_t value);
static void writeLe32(uint8_t* dst, uint32_t value);
static bool writeAll(FsFile& file, const void* data, size_t size);
static bool makeScreenshotPath(char* path, size_t pathSize);
static uint8_t expand5(uint16_t value);
static uint8_t expand6(uint16_t value);

} // namespace

GuiDisplay::GuiDisplay(M5GFX& display) : display_(display), canvas_(&display) {}

bool GuiDisplay::beginMirror()
{
    return ensureCanvas();
}

void GuiDisplay::pollScreenshotRequest()
{
    if (Serial.available() <= 0)
    {
        return;
    }

    const int key = Serial.read();
    if (key == 's' || key == 'S')
    {
        requestScreenshot();
    }
}

void GuiDisplay::requestScreenshot()
{
    screenshotRequested_ = true;
}

bool GuiDisplay::saveScreenshotIfRequested()
{
    if (!screenshotRequested_)
    {
        return false;
    }

    screenshotRequested_ = false;

    if (!ensureCanvas())
    {
        DBG_LOGE(TAG, "screenshot skipped: framebuffer unavailable");
        return false;
    }

    if (!MicroSdCard::isReady())
    {
        DBG_LOGE(TAG, "screenshot skipped: microSD is not ready");
        return false;
    }

    MicroSdCard::fs().mkdir(kScreenshotDir);

    char path[96] = {};
    if (!makeScreenshotPath(path, sizeof(path)))
    {
        DBG_LOGE(TAG, "screenshot skipped: failed to make filename");
        return false;
    }

    FsFile file;
    if (!file.open(path, O_WRONLY | O_CREAT | O_TRUNC))
    {
        DBG_LOGE(TAG, "screenshot open failed: %s", path);
        return false;
    }

    const int32_t  w          = canvas_.width();
    const int32_t  h          = canvas_.height();
    const uint32_t rowStride  = static_cast<uint32_t>((w * 3 + 3) & ~3);
    const uint32_t imageBytes = rowStride * static_cast<uint32_t>(h);
    const uint32_t fileBytes  = kBmpHeaderBytes + imageBytes;

    uint8_t header[kBmpHeaderBytes] = {};
    header[0]                       = 'B';
    header[1]                       = 'M';
    writeLe32(header + 2, fileBytes);
    writeLe32(header + 10, kBmpHeaderBytes);
    writeLe32(header + 14, kBmpInfoHeaderBytes);
    writeLe32(header + 18, static_cast<uint32_t>(w));
    writeLe32(header + 22, static_cast<uint32_t>(h));
    writeLe16(header + 26, kBmpPlanes);
    writeLe16(header + 28, kBmpBitsPerPixel);
    writeLe32(header + 34, imageBytes);
    writeLe32(header + 38, kBmpPelsPerMeter);
    writeLe32(header + 42, kBmpPelsPerMeter);

    uint16_t* pixels = new (std::nothrow) uint16_t[static_cast<size_t>(w)];
    uint8_t*  row    = new (std::nothrow) uint8_t[rowStride];

    bool ok = pixels != nullptr && row != nullptr && writeAll(file, header, sizeof(header));

    for (int32_t y = h - 1; ok && y >= 0; --y)
    {
        memset(row, 0, rowStride);
        canvas_.readRect(0, y, w, 1, pixels);

        for (int32_t x = 0; x < w; ++x)
        {
            const uint16_t color = pixels[x];
            uint8_t*       dst   = row + x * 3;
            dst[0]              = expand5(color & 0x1F);
            dst[1]              = expand6((color >> 5) & 0x3F);
            dst[2]              = expand5((color >> 11) & 0x1F);
        }

        ok = writeAll(file, row, rowStride);
    }

    delete[] row;
    delete[] pixels;

    file.close();

    if (!ok)
    {
        DBG_LOGE(TAG, "screenshot write failed: %s", path);
        return false;
    }

    DBG_LOGI(TAG, "screenshot saved: %s", path);
    return true;
}

void GuiDisplay::setColorDepth(int bits)
{
    display_.setColorDepth(bits);
    canvasReady_           = false;
    canvasAllocationError_ = false;
    recreateCanvas();
}

void GuiDisplay::setColorDepth(lgfx::color_depth_t depth)
{
    display_.setColorDepth(depth);
    canvasReady_           = false;
    canvasAllocationError_ = false;
    recreateCanvas();
}

bool GuiDisplay::ensureCanvas()
{
    if (canvasReady_)
    {
        return true;
    }

    if (canvasAllocationError_)
    {
        return false;
    }

    return recreateCanvas();
}

bool GuiDisplay::recreateCanvas()
{
    canvas_.deleteSprite();
    canvasReady_ = false;

    const int32_t w = display_.width();
    const int32_t h = display_.height();
    if (w <= 0 || h <= 0)
    {
        return false;
    }

    canvas_.setPsram(true);
    canvas_.setColorDepth(display_.getColorDepth());
    canvas_.setRotation(display_.getRotation());

    void* buffer = canvas_.createSprite(w, h);
    if (buffer == nullptr)
    {
        canvasAllocationError_ = true;
        DBG_LOGE(TAG, "framebuffer allocation failed: %ldx%ld", static_cast<long>(w), static_cast<long>(h));
        return false;
    }

    canvas_.fillScreen(TFT_BLACK);
    canvasReady_ = true;
    return true;
}

namespace
{

void writeLe16(uint8_t* dst, uint16_t value)
{
    dst[0] = static_cast<uint8_t>(value & 0xFF);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void writeLe32(uint8_t* dst, uint32_t value)
{
    dst[0] = static_cast<uint8_t>(value & 0xFF);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dst[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dst[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

bool writeAll(FsFile& file, const void* data, size_t size)
{
    return file.write(static_cast<const uint8_t*>(data), size) == size;
}

bool makeScreenshotPath(char* path, size_t pathSize)
{
    if (path == nullptr || pathSize == 0)
    {
        return false;
    }

    m5::rtc_datetime_t now = {};
    if (!Clock.getDateTime(&now))
    {
        now.date.year    = 2026;
        now.date.month   = 1;
        now.date.date    = 1;
        now.time.hours   = 0;
        now.time.minutes = 0;
        now.time.seconds = 0;
    }

    const int written = snprintf(path,
                                 pathSize,
                                 "%s/%04u%02u%02u_%02u%02u%02u_%03lu.bmp",
                                 kScreenshotDir,
                                 static_cast<unsigned>(now.date.year),
                                 static_cast<unsigned>(now.date.month),
                                 static_cast<unsigned>(now.date.date),
                                 static_cast<unsigned>(now.time.hours),
                                 static_cast<unsigned>(now.time.minutes),
                                 static_cast<unsigned>(now.time.seconds),
                                 static_cast<unsigned long>(millis() % 1000));
    return written > 0 && static_cast<size_t>(written) < pathSize;
}

uint8_t expand5(uint16_t value)
{
    return static_cast<uint8_t>((value * 255U + 15U) / 31U);
}

uint8_t expand6(uint16_t value)
{
    return static_cast<uint8_t>((value * 255U + 31U) / 63U);
}

} // namespace

#endif
