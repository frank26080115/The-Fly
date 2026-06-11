#pragma once

#include "conf.h"

#include <M5GFX.h>
#include <stddef.h>
#include <stdint.h>

#ifdef TEST_BUILD_SCREENSHOT

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

class GuiDisplay
{
public:
    explicit GuiDisplay(M5GFX& display);

    M5GFX& display()
    {
        return display_;
    }

    const M5GFX& display() const
    {
        return display_;
    }

    M5Canvas& canvas()
    {
        return canvas_;
    }

    operator M5GFX&()
    {
        return display_;
    }

    operator const M5GFX&() const
    {
        return display_;
    }

    bool beginMirror();
    void pollScreenshotRequest();
    void requestScreenshot();
    bool saveScreenshotIfRequested();

    int32_t width() const
    {
        return display_.width();
    }

    int32_t height() const
    {
        return display_.height();
    }

    lgfx::color_depth_t getColorDepth() const
    {
        return display_.getColorDepth();
    }

    void setBrightness(uint8_t brightness)
    {
        display_.setBrightness(brightness);
    }

    void setColorDepth(int bits);
    void setColorDepth(lgfx::color_depth_t depth);

    static constexpr uint16_t color565(uint8_t red, uint8_t green, uint8_t blue)
    {
        return M5GFX::color565(red, green, blue);
    }

    void startWrite(bool transaction = true)
    {
        display_.startWrite(transaction);
        if (ensureCanvas())
        {
            canvas_.startWrite(transaction);
        }
    }

    void endWrite()
    {
        display_.endWrite();
        if (canvasReady_)
        {
            canvas_.endWrite();
        }
    }

    void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
    {
        display_.setAddrWindow(x, y, w, h);
        if (ensureCanvas())
        {
            canvas_.setAddrWindow(x, y, w, h);
        }
    }

    template <typename T>
    void writePixels(const T* data, int32_t len)
    {
        display_.writePixels(data, len);
        if (ensureCanvas())
        {
            canvas_.writePixels(data, len);
        }
    }

    void writePixels(const uint16_t* data, int32_t len, bool swap)
    {
        display_.writePixels(data, len, swap);
        if (ensureCanvas())
        {
            canvas_.writePixels(data, len, swap);
        }
    }

    void writePixels(const void* data, int32_t len, bool swap)
    {
        display_.writePixels(data, len, swap);
        if (ensureCanvas())
        {
            canvas_.writePixels(data, len, swap);
        }
    }

    template <typename T>
    void fillScreen(const T& color)
    {
        display_.fillScreen(color);
        if (ensureCanvas())
        {
            canvas_.fillScreen(color);
        }
    }

    void fillScreen()
    {
        display_.fillScreen();
        if (ensureCanvas())
        {
            canvas_.fillScreen();
        }
    }

    template <typename T>
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, const T& color)
    {
        display_.fillRect(x, y, w, h, color);
        if (ensureCanvas())
        {
            canvas_.fillRect(x, y, w, h, color);
        }
    }

    template <typename T>
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, const T& color)
    {
        display_.drawRect(x, y, w, h, color);
        if (ensureCanvas())
        {
            canvas_.drawRect(x, y, w, h, color);
        }
    }

    template <typename T>
    void drawPixel(int32_t x, int32_t y, const T& color)
    {
        display_.drawPixel(x, y, color);
        if (ensureCanvas())
        {
            canvas_.drawPixel(x, y, color);
        }
    }

    template <typename T>
    void drawFastHLine(int32_t x, int32_t y, int32_t w, const T& color)
    {
        display_.drawFastHLine(x, y, w, color);
        if (ensureCanvas())
        {
            canvas_.drawFastHLine(x, y, w, color);
        }
    }

    template <typename T>
    void drawFastVLine(int32_t x, int32_t y, int32_t h, const T& color)
    {
        display_.drawFastVLine(x, y, h, color);
        if (ensureCanvas())
        {
            canvas_.drawFastVLine(x, y, h, color);
        }
    }

    template <typename T>
    void setTextColor(const T& color)
    {
        display_.setTextColor(color);
        if (ensureCanvas())
        {
            canvas_.setTextColor(color);
        }
    }

    template <typename T1, typename T2>
    void setTextColor(const T1& fgcolor, const T2& bgcolor)
    {
        display_.setTextColor(fgcolor, bgcolor);
        if (ensureCanvas())
        {
            canvas_.setTextColor(fgcolor, bgcolor);
        }
    }

    void setTextDatum(uint8_t datum)
    {
        display_.setTextDatum(datum);
        if (ensureCanvas())
        {
            canvas_.setTextDatum(datum);
        }
    }

    void setTextDatum(lgfx::textdatum_t datum)
    {
        display_.setTextDatum(datum);
        if (ensureCanvas())
        {
            canvas_.setTextDatum(datum);
        }
    }

    void setTextSize(float size)
    {
        display_.setTextSize(size);
        if (ensureCanvas())
        {
            canvas_.setTextSize(size);
        }
    }

    void setTextSize(float sizeX, float sizeY)
    {
        display_.setTextSize(sizeX, sizeY);
        if (ensureCanvas())
        {
            canvas_.setTextSize(sizeX, sizeY);
        }
    }

    void setTextFont(int font)
    {
        display_.setTextFont(font);
        if (ensureCanvas())
        {
            canvas_.setTextFont(font);
        }
    }

    void setTextFont(const lgfx::IFont* font = nullptr)
    {
        display_.setTextFont(font);
        if (ensureCanvas())
        {
            canvas_.setTextFont(font);
        }
    }

    int32_t fontHeight() const
    {
        return display_.fontHeight();
    }

    int32_t fontHeight(uint8_t font) const
    {
        return display_.fontHeight(font);
    }

    int32_t fontHeight(const lgfx::IFont* font) const
    {
        return display_.fontHeight(font);
    }

    int32_t textWidth(const char* text)
    {
        return display_.textWidth(text);
    }

    int32_t textWidth(const String& text)
    {
        return display_.textWidth(text);
    }

    size_t drawString(const char* text, int32_t x, int32_t y)
    {
        const size_t written = display_.drawString(text, x, y);
        if (ensureCanvas())
        {
            canvas_.drawString(text, x, y);
        }
        return written;
    }

    size_t drawString(const char* text, int32_t x, int32_t y, uint8_t font)
    {
        const size_t written = display_.drawString(text, x, y, font);
        if (ensureCanvas())
        {
            canvas_.drawString(text, x, y, font);
        }
        return written;
    }

    size_t drawString(const char* text, int32_t x, int32_t y, const lgfx::IFont* font)
    {
        const size_t written = display_.drawString(text, x, y, font);
        if (ensureCanvas())
        {
            canvas_.drawString(text, x, y, font);
        }
        return written;
    }

    size_t drawString(const String& text, int32_t x, int32_t y)
    {
        const size_t written = display_.drawString(text, x, y);
        if (ensureCanvas())
        {
            canvas_.drawString(text, x, y);
        }
        return written;
    }

    size_t drawString(const String& text, int32_t x, int32_t y, uint8_t font)
    {
        const size_t written = display_.drawString(text, x, y, font);
        if (ensureCanvas())
        {
            canvas_.drawString(text, x, y, font);
        }
        return written;
    }

    size_t drawString(const String& text, int32_t x, int32_t y, const lgfx::IFont* font)
    {
        const size_t written = display_.drawString(text, x, y, font);
        if (ensureCanvas())
        {
            canvas_.drawString(text, x, y, font);
        }
        return written;
    }

    void qrcode(const char* text, int32_t x = -1, int32_t y = -1, int32_t size = -1, uint8_t version = 1,
                bool margin = false)
    {
        display_.qrcode(text, x, y, size, version, margin);
        if (ensureCanvas())
        {
            canvas_.qrcode(text, x, y, size, version, margin);
        }
    }

    void qrcode(const String& text, int32_t x = -1, int32_t y = -1, int32_t size = -1, uint8_t version = 1)
    {
        display_.qrcode(text, x, y, size, version);
        if (ensureCanvas())
        {
            canvas_.qrcode(text, x, y, size, version);
        }
    }

private:
    bool ensureCanvas();
    bool recreateCanvas();
    bool ensureScreenshotCanvas();
    bool recreateScreenshotCanvas();
    bool captureScreenshotBuffer();
    bool saveScreenshotBuffer();

    M5GFX&   display_;
    M5Canvas canvas_;
    M5Canvas screenshotCanvas_;
    bool     canvasReady_                     = false;
    bool     canvasAllocationError_           = false;
    bool     screenshotCanvasReady_           = false;
    bool     screenshotCanvasAllocationError_ = false;
    bool     screenshotRequested_             = false;
    bool     screenshotSavePending_           = false;
};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#else

using GuiDisplay = M5GFX;

#endif
