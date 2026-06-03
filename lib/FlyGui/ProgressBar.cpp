#include "ProgressBar.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

namespace
{
constexpr uint16_t kHueRed   = 0;
constexpr uint16_t kHueGreen = 120;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

uint16_t hsv_to_rgb565(uint16_t hue, uint8_t saturation, uint8_t value);
} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

ProgressBar::ProgressBar(int16_t x, int16_t y, int16_t width, int16_t height) : FlyGuiItem(x, y, width, height) {}

void ProgressBar::firstDraw()
{
    if (!visible())
    {
        return;
    }

    drawFrame();
    drawFill();
    drawn_ = true;
    markClean();
}

void ProgressBar::update(float progress)
{
    const float nextProgress = normalizeProgress(progress);
    if (nextProgress == progress_ && drawn_ && !dirty())
    {
        return;
    }

    progress_ = nextProgress;
    if (!drawn_ || dirty())
    {
        firstDraw();
        return;
    }

    drawFill();
}

void ProgressBar::onLoad()
{
    drawn_ = false;
    FlyGuiItem::onLoad();
}

bool ProgressBar::redraw(bool forced)
{
    if (!visible() || (!forced && !dirty()))
    {
        return false;
    }

    firstDraw();
    return true;
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

float ProgressBar::normalizeProgress(float progress)
{
    if (progress <= 0.0f)
    {
        return 0.0f;
    }

    if (progress >= 100.0f)
    {
        return 100.0f;
    }

    return progress;
}

void ProgressBar::drawFrame() const
{
    if (width() <= 0 || height() <= 0)
    {
        return;
    }

    thefly_display.drawFastHLine(x(), y(), width(), TFT_WHITE);
    thefly_display.drawFastHLine(x(), static_cast<int16_t>(y() + height() - 1), width(), TFT_WHITE);
    thefly_display.drawFastVLine(x(), y(), height(), TFT_WHITE);
    thefly_display.drawFastVLine(static_cast<int16_t>(x() + width() - 1), y(), height(), TFT_WHITE);
}

void ProgressBar::drawFill() const
{
    if (width() <= 2 || height() <= 2)
    {
        return;
    }

    const int16_t innerX      = static_cast<int16_t>(x() + 1);
    const int16_t innerY      = static_cast<int16_t>(y() + 1);
    const int16_t innerWidth  = static_cast<int16_t>(width() - 2);
    const int16_t innerHeight = static_cast<int16_t>(height() - 2);
    const int16_t filled      = filledWidth();
    const int16_t empty       = static_cast<int16_t>(innerWidth - filled);

    if (filled > 0)
    {
        thefly_display.fillRect(innerX, innerY, filled, innerHeight, fillColor());
    }
    if (empty > 0)
    {
        thefly_display.fillRect(static_cast<int16_t>(innerX + filled), innerY, empty, innerHeight, TFT_BLACK);
    }
}

int16_t ProgressBar::filledWidth() const
{
    const int16_t innerWidth = width() > 2 ? static_cast<int16_t>(width() - 2) : 0;
    if (progress_ <= 0.0f)
    {
        return 0;
    }

    if (progress_ >= 100.0f)
    {
        return innerWidth;
    }

    return static_cast<int16_t>((static_cast<float>(innerWidth) * progress_) / 100.0f);
}

uint16_t ProgressBar::fillColor() const
{
    if (progress_ >= 100.0f)
    {
        return hsv_to_rgb565(kHueGreen, 255, 255);
    }

    const uint16_t hue =
        progress_ >= 50.0f
            ? kHueGreen
            : static_cast<uint16_t>(kHueRed + ((static_cast<float>(kHueGreen - kHueRed) * progress_) / 50.0f));
    return hsv_to_rgb565(hue, 255, 255);
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

namespace
{
uint16_t hsv_to_rgb565(uint16_t hue, uint8_t saturation, uint8_t value)
{
    hue %= 360U;

    const uint8_t region    = hue / 60U;
    const uint8_t remainder = static_cast<uint8_t>(((hue % 60U) * 255U) / 60U);

    const uint8_t p = static_cast<uint8_t>((static_cast<uint16_t>(value) * (255U - saturation)) / 255U);
    const uint8_t q = static_cast<uint8_t>(
        (static_cast<uint16_t>(value) * (255U - ((static_cast<uint16_t>(saturation) * remainder) / 255U))) / 255U);
    const uint8_t t = static_cast<uint8_t>(
        (static_cast<uint16_t>(value) * (255U - ((static_cast<uint16_t>(saturation) * (255U - remainder)) / 255U))) /
        255U);

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    switch (region)
    {
    case 0:
        r = value;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = value;
        b = p;
        break;
    case 2:
        r = p;
        g = value;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = value;
        break;
    case 4:
        r = t;
        g = p;
        b = value;
        break;
    default:
        r = value;
        g = p;
        b = q;
        break;
    }

    return thefly_display.color565(r, g, b);
}
} // namespace
