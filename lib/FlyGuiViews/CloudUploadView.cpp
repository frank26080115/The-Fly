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

int16_t progress_x()
{
    return static_cast<int16_t>((thefly_display.width() - kProgressWidth) / 2);
}

float progress_percent(uint64_t bytes_uploaded, uint64_t bytes_total)
{
    if (bytes_total == 0)
    {
        return 0.0f;
    }

    const float percent = (static_cast<float>(bytes_uploaded) * 100.0f) / static_cast<float>(bytes_total);
    if (percent <= 0.0f)
    {
        return 0.0f;
    }

    return percent >= 100.0f ? 100.0f : percent;
}
} // namespace

CloudUploadView::CloudUploadView(FlyGuiItemCallback cancelCallback)
    : ConnWaitingView(CONN_WAITING_CLOUD, "", cancelCallback, FLYGUI_VIEW_UPLOAD_PROGRESS),
      progressBar_(0, kProgressY, kProgressWidth, kProgressHeight)
{
    addItem(progressBar_);
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
    progressBar_.relocate(progress_x(), kProgressY, kProgressWidth, kProgressHeight);
    progressBar_.update(progressPercent());

    const uint8_t percent = roundedProgressPercent();
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

float CloudUploadView::progressPercent() const
{
    return progress_percent(status_.bytes_uploaded, status_.bytes_total);
}

uint8_t CloudUploadView::roundedProgressPercent() const
{
    const float percent = progressPercent();
    if (percent <= 0.0f)
    {
        return 0;
    }

    if (percent >= 100.0f)
    {
        return 100;
    }

    return static_cast<uint8_t>(percent + 0.5f);
}
