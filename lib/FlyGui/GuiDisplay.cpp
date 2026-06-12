#include "GuiDisplay.h"

#ifdef TEST_BUILD_SCREENSHOT

#include "AudioFileRecorder.h"
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
constexpr uint32_t    kBmpBytesPerPixel   = kBmpBitsPerPixel / 8;
constexpr uint32_t    kBmpInfoHeaderBytes = 40;
constexpr uint16_t    kBmpPlanes          = 1;
constexpr uint32_t    kBmpPelsPerMeter    = 2835;

static void writeLe16(uint8_t* dst, uint16_t value);
static void writeLe32(uint8_t* dst, uint32_t value);
static bool writeAll(FsFile& file, const void* data, size_t size);
static bool makeScreenshotPath(char* path, size_t pathSize);
static void swapRgbToBgr(uint8_t* row, int32_t pixelCount);

} // namespace

GuiDisplay::GuiDisplay(M5GFX& display) : display_(display), canvas_(&display), screenshotCanvas_(&display) {}

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
    if (screenshotRequested_)
    {
        screenshotRequested_ = false;
        if (captureScreenshotBuffer())
        {
            screenshotSavePending_ = true;
            if (AudioFileRecorder::isRecording())
            {
                DBG_LOGI(TAG, "screenshot captured; save deferred until recording stops");
            }
        }
    }

    if (!screenshotSavePending_)
    {
        return false;
    }

    if (AudioFileRecorder::isRecording())
    {
        return false;
    }

    return saveScreenshotBuffer();
}

void GuiDisplay::setColorDepth(int bits)
{
    display_.setColorDepth(bits);
    canvasReady_                     = false;
    canvasAllocationError_           = false;
    screenshotCanvasReady_           = false;
    screenshotCanvasAllocationError_ = false;
    screenshotSavePending_           = false;
    recreateCanvas();
}

void GuiDisplay::setColorDepth(lgfx::color_depth_t depth)
{
    display_.setColorDepth(depth);
    canvasReady_                     = false;
    canvasAllocationError_           = false;
    screenshotCanvasReady_           = false;
    screenshotCanvasAllocationError_ = false;
    screenshotSavePending_           = false;
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
    screenshotCanvas_.deleteSprite();
    canvasReady_                     = false;
    screenshotCanvasReady_           = false;
    screenshotCanvasAllocationError_ = false;
    screenshotSavePending_           = false;

    const int32_t w = display_.width();
    const int32_t h = display_.height();
    if (w <= 0 || h <= 0)
    {
        return false;
    }

    canvas_.setPsram(true);
    canvas_.setColorDepth(display_.getColorDepth());
    canvas_.setRotation(0);

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

bool GuiDisplay::ensureScreenshotCanvas()
{
    const bool hasBmpDepth =
        (screenshotCanvas_.getColorDepth() & lgfx::color_depth_t::bit_mask) == kBmpBitsPerPixel;

    if (screenshotCanvasReady_ && screenshotCanvas_.width() == canvas_.width() &&
        screenshotCanvas_.height() == canvas_.height() && hasBmpDepth)
    {
        return true;
    }

    if (screenshotCanvasAllocationError_)
    {
        return false;
    }

    return recreateScreenshotCanvas();
}

bool GuiDisplay::recreateScreenshotCanvas()
{
    if (!ensureCanvas())
    {
        return false;
    }

    screenshotCanvas_.deleteSprite();
    screenshotCanvasReady_ = false;

    screenshotCanvas_.setPsram(true);
    screenshotCanvas_.setColorDepth(kBmpBitsPerPixel);
    screenshotCanvas_.setRotation(0);

    void* buffer = screenshotCanvas_.createSprite(canvas_.width(), canvas_.height());
    if (buffer == nullptr)
    {
        screenshotCanvasAllocationError_ = true;
        DBG_LOGE(TAG,
                 "screenshot buffer allocation failed: %ldx%ld",
                 static_cast<long>(canvas_.width()),
                 static_cast<long>(canvas_.height()));
        return false;
    }

    screenshotCanvasReady_ = true;
    return true;
}

bool GuiDisplay::captureScreenshotBuffer()
{
    if (!ensureCanvas() || !ensureScreenshotCanvas())
    {
        DBG_LOGE(TAG, "screenshot capture skipped: framebuffer unavailable");
        return false;
    }

    const int32_t w = canvas_.width();
    const int32_t h = canvas_.height();
    if (w <= 0 || h <= 0)
    {
        DBG_LOGE(TAG, "screenshot capture skipped: invalid framebuffer size");
        return false;
    }

    uint8_t* screenshotBuffer = static_cast<uint8_t*>(screenshotCanvas_.getBuffer());
    if (screenshotBuffer == nullptr)
    {
        DBG_LOGE(TAG, "screenshot capture skipped: screenshot buffer unavailable");
        return false;
    }

    const uint32_t rowBytes = static_cast<uint32_t>(w) * kBmpBytesPerPixel;
    for (int32_t y = 0; y < h; ++y)
    {
        uint8_t* row = screenshotBuffer + static_cast<size_t>(y) * rowBytes;
        canvas_.readRectRGB(0, y, w, 1, row);
        swapRgbToBgr(row, w);
    }

    return true;
}

bool GuiDisplay::saveScreenshotBuffer()
{
    if (!MicroSdCard::isReady())
    {
        return false;
    }

    MicroSdCard::fs().mkdir(kScreenshotDir);

    char path[96] = {};
    if (!makeScreenshotPath(path, sizeof(path)))
    {
        DBG_LOGE(TAG, "screenshot skipped: failed to make filename");
        screenshotSavePending_ = false;
        return false;
    }

    FsFile file;
    if (!file.open(path, O_WRONLY | O_CREAT | O_TRUNC))
    {
        DBG_LOGE(TAG, "screenshot open failed: %s", path);
        screenshotSavePending_ = false;
        return false;
    }

    const int32_t  w          = screenshotCanvas_.width();
    const int32_t  h          = screenshotCanvas_.height();
    const uint32_t rowBytes   = static_cast<uint32_t>(w) * kBmpBytesPerPixel;
    const uint32_t rowStride  = static_cast<uint32_t>((w * kBmpBytesPerPixel + 3) & ~3);
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

    const uint8_t* screenshotBuffer = static_cast<const uint8_t*>(screenshotCanvas_.getBuffer());
    uint8_t*       row              = rowStride == rowBytes ? nullptr : new (std::nothrow) uint8_t[rowStride];

    bool ok = screenshotBuffer != nullptr && (rowStride == rowBytes || row != nullptr) && writeAll(file, header, sizeof(header));
    if (screenshotBuffer == nullptr)
    {
        DBG_LOGE(TAG, "screenshot write failed: screenshot buffer unavailable");
    }
    else if (rowStride != rowBytes && row == nullptr)
    {
        DBG_LOGE(TAG, "screenshot write failed: row buffer allocation failed");
    }

    for (int32_t y = h - 1; ok && y >= 0; --y)
    {
        const uint8_t* src = screenshotBuffer + static_cast<size_t>(y) * rowBytes;
        if (rowStride == rowBytes)
        {
            ok = writeAll(file, src, rowBytes);
        }
        else
        {
            memset(row, 0, rowStride);
            memcpy(row, src, rowBytes);
            ok = writeAll(file, row, rowStride);
        }
    }

    delete[] row;

    file.close();

    if (!ok)
    {
        DBG_LOGE(TAG, "screenshot write failed: %s", path);
        screenshotSavePending_ = false;
        return false;
    }

    screenshotSavePending_ = false;
    DBG_LOGI(TAG, "screenshot saved: %s", path);
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

void swapRgbToBgr(uint8_t* row, int32_t pixelCount)
{
    for (int32_t x = 0; x < pixelCount; ++x)
    {
        uint8_t* pixel = row + static_cast<size_t>(x) * kBmpBytesPerPixel;
        const uint8_t red = pixel[0];
        pixel[0] = pixel[2];
        pixel[2] = red;
    }
}

} // namespace

#endif
