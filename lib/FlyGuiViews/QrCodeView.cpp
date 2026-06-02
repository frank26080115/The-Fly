#include "QrCodeView.h"

#include "Display.h"
#include <string.h>

namespace
{
constexpr int16_t kRectY = FlyGui::kTopBarHeight + 1;
} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

QrCodeView::QrCodeView() : FlyGuiView(FLYGUI_VIEW_QR_CODE) {}

void QrCodeView::configure(const char* text)
{
    strlcpy(text_, text ? text : "", sizeof(text_));
    setDirty();
}

void QrCodeView::onLoad()
{
    FlyGuiView::onLoad();
    drawQrCode();
}

bool QrCodeView::handleTouch(const FlyGuiTouchEvent& event)
{
    if (event.justPressed || event.justReleased)
    {
        dismiss();
    }

    return true;
}

void QrCodeView::redraw(bool forced)
{
    if (!forced && !dirty())
    {
        return;
    }

    drawQrCode();
}

void QrCodeView::onPressLeft()
{
    dismiss();
}

void QrCodeView::onPressMid()
{
    dismiss();
}

void QrCodeView::onPressRight()
{
    dismiss();
}

void QrCodeView::dismiss()
{
    FlyGui* owner = gui();
    if (owner)
    {
        owner->showView(FLYGUI_VIEW_AP_MODE);
    }
}

void QrCodeView::drawQrCode()
{
    const int16_t rect_h = static_cast<int16_t>(thefly_display.height() - kRectY);
    if (rect_h <= 0)
    {
        return;
    }

    thefly_display.fillRect(0, kRectY, thefly_display.width(), rect_h, TFT_WHITE);
    if (text_[0] == '\0')
    {
        markClean();
        return;
    }

    const int16_t qr_size = thefly_display.width() < rect_h ? thefly_display.width() : rect_h;
    const int16_t qr_x    = static_cast<int16_t>((thefly_display.width() - qr_size) / 2);
    const int16_t qr_y    = static_cast<int16_t>(kRectY + ((rect_h - qr_size) / 2));
    thefly_display.qrcode(text_, qr_x, qr_y, qr_size, 1, true);
    markClean();
}
