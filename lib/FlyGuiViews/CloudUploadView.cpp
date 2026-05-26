#include "CloudUploadView.h"

#include <stdio.h>
#include <string.h>

#include "Display.h"

namespace
{
constexpr int16_t kProgressWidth       = 300;
constexpr int16_t kProgressHeight      = 12;
constexpr int16_t kProgressY           = FlyGui::kTopBarHeight + 129;
constexpr int16_t kProgressTextY       = kProgressY + kProgressHeight + 4;
constexpr int16_t kProgressTextHeight  = 18;
constexpr uint8_t kTextFont            = 2;
constexpr uint8_t kSmallTextFont       = 1;
constexpr float   kTextSize            = 1.0f;
constexpr uint16_t kHueRed             = 0;
constexpr uint16_t kHueGreen           = 120;

int16_t progress_x()
{
    return static_cast<int16_t>((thefly_display.width() - kProgressWidth) / 2);
}

uint8_t clamp_percent(uint64_t bytes_uploaded, uint64_t bytes_total)
{
    if (bytes_total == 0)
    {
        return 0;
    }

    const uint64_t percent = (bytes_uploaded * 100ULL) / bytes_total;
    return percent > 100ULL ? 100 : static_cast<uint8_t>(percent);
}

uint16_t hsv_to_rgb565(uint16_t hue, uint8_t saturation, uint8_t value)
{
    hue %= 360U;

    const uint8_t region    = hue / 60U;
    const uint8_t remainder = static_cast<uint8_t>(((hue % 60U) * 255U) / 60U);

    const uint8_t p = static_cast<uint8_t>((static_cast<uint16_t>(value) * (255U - saturation)) / 255U);
    const uint8_t q = static_cast<uint8_t>((static_cast<uint16_t>(value) * (255U - ((static_cast<uint16_t>(saturation) * remainder) / 255U))) / 255U);
    const uint8_t t = static_cast<uint8_t>((static_cast<uint16_t>(value) * (255U - ((static_cast<uint16_t>(saturation) * (255U - remainder)) / 255U))) / 255U);

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

CloudUploadView::CloudUploadView(FlyGuiItemCallback cancelCallback)
    : ConnWaitingView(CONN_WAITING_CLOUD, "", cancelCallback, FLYGUI_VIEW_UPLOAD_PROGRESS)
{
}

void CloudUploadView::configureUpload(CloudUpload* uploader, const char* targetName)
{
    uploader_ = uploader;
    status_   = {};
    configure(CONN_WAITING_CLOUD, targetName ? targetName : "");
}

void CloudUploadView::updateProgress(const CloudUpload::Status& status)
{
    status_ = status;
}

void CloudUploadView::drawBottomCenter()
{
    thefly_display.fillRect(64, 180, 192, 60, TFT_BLACK);
}

bool CloudUploadView::updateHourglass(uint32_t now, bool forced)
{
    const bool updated = ConnWaitingView::updateHourglass(now, forced);
    if (!forced && !updated)
    {
        return false;
    }

    if (uploader_)
    {
        updateProgress(uploader_->status());
    }

    drawProgress();
    return true;
}

void CloudUploadView::drawProgress()
{
    const int16_t x = progress_x();
    const int16_t inner_x = static_cast<int16_t>(x + 1);
    const int16_t inner_y = static_cast<int16_t>(kProgressY + 1);
    const int16_t inner_width = static_cast<int16_t>(kProgressWidth - 2);
    const int16_t inner_height = static_cast<int16_t>(kProgressHeight - 2);
    const uint8_t percent = progressPercent();
    const int16_t filled_width = static_cast<int16_t>((static_cast<int32_t>(inner_width) * percent) / 100);
    const int16_t empty_width = static_cast<int16_t>(inner_width - filled_width);

    thefly_display.drawFastHLine(x, kProgressY, kProgressWidth, TFT_WHITE);
    thefly_display.drawFastHLine(x, static_cast<int16_t>(kProgressY + kProgressHeight - 1), kProgressWidth, TFT_WHITE);
    thefly_display.drawFastVLine(x, kProgressY, kProgressHeight, TFT_WHITE);
    thefly_display.drawFastVLine(static_cast<int16_t>(x + kProgressWidth - 1), kProgressY, kProgressHeight, TFT_WHITE);

    if (filled_width > 0)
    {
        thefly_display.fillRect(inner_x, inner_y, filled_width, inner_height, progressColor());
    }
    if (empty_width > 0)
    {
        thefly_display.fillRect(static_cast<int16_t>(inner_x + filled_width), inner_y, empty_width, inner_height, TFT_BLACK);
    }

    char text[48];
    snprintf(text,
             sizeof(text),
             "%u%% %lu/%lu (%lu errs)",
             static_cast<unsigned>(percent),
             static_cast<unsigned long>(status_.files_succeeded),
             static_cast<unsigned long>(status_.files_total),
             static_cast<unsigned long>(status_.files_failed));

    thefly_display.fillRect(0, kProgressTextY, thefly_display.width(), kProgressTextHeight, TFT_BLACK);
    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);
    if (thefly_display.textWidth(text) <= thefly_display.width())
    {
        thefly_display.setTextDatum(top_center);
        thefly_display.drawString(text, thefly_display.width() / 2, kProgressTextY);
        return;
    }

    thefly_display.setTextFont(kSmallTextFont);
    thefly_display.setTextDatum(top_left);
    thefly_display.drawString(text, 1, kProgressTextY);
}

uint8_t CloudUploadView::progressPercent() const
{
    return clamp_percent(status_.bytes_uploaded, status_.bytes_total);
}

uint16_t CloudUploadView::progressColor() const
{
    const uint8_t percent = progressPercent();
    const uint16_t hue = percent >= 50 ? kHueGreen : static_cast<uint16_t>(kHueRed + ((static_cast<uint32_t>(kHueGreen - kHueRed) * percent) / 50U));
    return hsv_to_rgb565(hue, 255, 255);
}
