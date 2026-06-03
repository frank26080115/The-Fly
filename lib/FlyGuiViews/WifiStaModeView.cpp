#include "WifiStaModeView.h"

#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "Display.h"
#include "SpriteDraw.h"
#include "WifiManager.h"
#include "sprites.h"

namespace
{
constexpr int16_t     kContentY                 = FlyGui::kTopBarHeight;
constexpr int16_t     kWifiIconX                = 4;
constexpr int16_t     kWifiIconY                = FlyGui::kTopBarHeight + 2;
constexpr int16_t     kConnectionInfoX          = 108;
constexpr int16_t     kConnectionInfoY          = FlyGui::kTopBarHeight + 2;
constexpr int16_t     kConnectionInfoWidth      = 212;
constexpr int16_t     kConnectionInfoLineHeight = 16;
constexpr int16_t     kTextX                    = 8;
constexpr int16_t     kTextY                    = 68;
constexpr int16_t     kTextWidth                = 248;
constexpr int16_t     kLabelLineHeight          = 18;
constexpr int16_t     kValueLineHeight          = 28;
constexpr int16_t     kStatsY                   = 190;
constexpr int16_t     kSmallLineHeight          = 10;
constexpr int16_t     kDismissSize              = 50;
constexpr int16_t     kDismissX                 = 270;
constexpr int16_t     kDismissY                 = 190;
constexpr uint8_t     kNormalTextFont           = 2;
constexpr uint8_t     kConnectionInfoTextFont   = kNormalTextFont;
constexpr uint8_t     kValueTextFont            = 4;
constexpr uint8_t     kSmallTextFont            = 1;
constexpr uint32_t    kStatsCycleMs             = 3000;
constexpr const char* kStopHint                 = "reset or power-off to stop";

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void draw_fit_line(const char* text, int16_t x, int16_t y, int16_t width, uint8_t font, uint16_t color);
static void set_text_style(uint8_t font, uint16_t color);
static const wifi_item_t* current_station();
} // namespace

WifiStaModeView* WifiStaModeView::activeView_ = nullptr;

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

WifiStaModeView::WifiStaModeView()
    : FlyGuiView(FLYGUI_VIEW_STA_MODE), wifiIcon_(kWifiIconX, kWifiIconY, SPRITE_WIFI_50_WIDTH, SPRITE_WIFI_50_HEIGHT),
      dismissItem_(kDismissX, kDismissY, kDismissSize, kDismissSize)
{
    wifiIcon_.setSprite(sprite_wifi_50, SPRITE_WIFI_50_WIDTH, SPRITE_WIFI_50_HEIGHT, SPRITE_WIFI_50_BYTES);
    addItem(wifiIcon_);

    dismissItem_.setSprite(sprite_canceldoor_50,
                           SPRITE_CANCELDOOR_50_WIDTH,
                           SPRITE_CANCELDOOR_50_HEIGHT,
                           SPRITE_CANCELDOOR_50_BYTES);
    dismissItem_.setCallback(onDismissTriggered);
    addItem(dismissItem_);
}

void WifiStaModeView::configure(bool showDismissButton)
{
    showDismissButton_ = showDismissButton;
    dismissItem_.setVisible(showDismissButton_);
    setDirty();
}

void WifiStaModeView::onLoad()
{
    activeView_      = this;
    statsIndex_      = 0;
    lastStatsDrawMs_ = 0;
    dismissItem_.setVisible(showDismissButton_);
    FlyGuiView::onLoad();
}

void WifiStaModeView::onUnload()
{
    if (activeView_ == this)
    {
        activeView_ = nullptr;
    }
    FlyGuiView::onUnload();
}

bool WifiStaModeView::handleTouch(const FlyGuiTouchEvent& event)
{
    if (showDismissButton_ && (dismissItem_.contains(event.x, event.y) || dismissItem_.isPressed()))
    {
        return dismissItem_.handleTouch(event);
    }

    return false;
}

void WifiStaModeView::redraw(bool forced)
{
    const bool redrawStatic = forced || dirty();
    if (redrawStatic)
    {
        drawStaticContent();
        drawStatsLine(true);
        if (showDismissButton_)
        {
            dismissItem_.redraw(true);
        }
        markClean();
        return;
    }

    drawStatsLine(false);
}

void WifiStaModeView::onPressRight()
{
    if (showDismissButton_)
    {
        dismissItem_.trigger();
    }
}

void WifiStaModeView::onDismissTriggered(uint32_t)
{
    if (activeView_)
    {
        activeView_->dismiss();
    }
}

void WifiStaModeView::dismiss()
{
    FlyGui* owner = gui();
    if (owner)
    {
        owner->showView(FLYGUI_VIEW_SCROLL);
    }
}

void WifiStaModeView::drawStaticContent()
{
    thefly_display.fillRect(0,
                            kContentY,
                            thefly_display.width(),
                            static_cast<int16_t>(thefly_display.height() - kContentY),
                            TFT_BLACK);

    wifiIcon_.redraw(true);
    drawConnectionInfo();
    drawNetworkDetails();

    if (!showDismissButton_)
    {
        drawStopHint();
    }
}

void WifiStaModeView::drawConnectionInfo()
{
    thefly_display.fillRect(kConnectionInfoX,
                            kConnectionInfoY,
                            kConnectionInfoWidth,
                            static_cast<int16_t>(kConnectionInfoLineHeight * 2),
                            TFT_BLACK);

    draw_fit_line("Station Mode",
                  kConnectionInfoX,
                  kConnectionInfoY,
                  kConnectionInfoWidth,
                  kConnectionInfoTextFont,
                  TFT_WHITE);
    draw_fit_line("Connected",
                  kConnectionInfoX,
                  static_cast<int16_t>(kConnectionInfoY + kConnectionInfoLineHeight),
                  kConnectionInfoWidth,
                  kConnectionInfoTextFont,
                  TFT_WHITE);
}

void WifiStaModeView::drawNetworkDetails()
{
    const wifi_item_t* active         = current_station();
    const char*        ssid           = active && active->ssid[0] != '\0' ? active->ssid : "";
    const String       assignedIpText = WiFi.localIP().toString();
    const String       gatewayIpText  = WiFi.gatewayIP().toString();

    thefly_display.fillRect(kTextX, kTextY, kTextWidth, static_cast<int16_t>(kStatsY - kTextY), TFT_BLACK);

    int16_t y = kTextY;
    draw_fit_line("SSID:", kTextX, y, kTextWidth, kNormalTextFont, TFT_WHITE);
    y = static_cast<int16_t>(y + kLabelLineHeight);
    draw_fit_line(ssid, kTextX, y, kTextWidth, kValueTextFont, TFT_YELLOW);
    y = static_cast<int16_t>(y + kValueLineHeight);

    draw_fit_line("assigned IP:", kTextX, y, kTextWidth, kNormalTextFont, TFT_WHITE);
    y = static_cast<int16_t>(y + kLabelLineHeight);
    draw_fit_line(assignedIpText.c_str(), kTextX, y, kTextWidth, kNormalTextFont, TFT_YELLOW);
    y = static_cast<int16_t>(y + kLabelLineHeight);

    draw_fit_line("gateway IP:", kTextX, y, kTextWidth, kNormalTextFont, TFT_WHITE);
    y = static_cast<int16_t>(y + kLabelLineHeight);
    draw_fit_line(gatewayIpText.c_str(), kTextX, y, kTextWidth, kNormalTextFont, TFT_YELLOW);
}

void WifiStaModeView::drawStopHint()
{
    const int16_t hintY = static_cast<int16_t>(thefly_display.height() - kSmallLineHeight);
    thefly_display.fillRect(kTextX, hintY, thefly_display.width() - kTextX, kSmallLineHeight, TFT_BLACK);
    draw_fit_line(kStopHint,
                  kTextX,
                  hintY,
                  static_cast<int16_t>(thefly_display.width() - (kTextX * 2)),
                  kSmallTextFont,
                  TFT_RED);
}

void WifiStaModeView::drawStatsLine(bool forced)
{
    const uint32_t now = millis();
#if BUILD_WITH_SECURITY_LEVEL <= 0
    constexpr uint8_t kStatsCount = 4;
#else
    constexpr uint8_t kStatsCount = 5;
#endif

    if (!forced && now - lastStatsDrawMs_ < kStatsCycleMs)
    {
        return;
    }

    if (forced)
    {
        statsIndex_ = 0;
    }
    else
    {
        statsIndex_ = static_cast<uint8_t>((statsIndex_ + 1) % kStatsCount);
    }
    lastStatsDrawMs_ = now;

    char text[32] = {};
    formatStatsLine(text, sizeof(text));
    thefly_display.fillRect(kTextX, kStatsY, kTextWidth, kSmallLineHeight, TFT_BLACK);
    draw_fit_line(text, kTextX, kStatsY, kTextWidth, kSmallTextFont, TFT_WHITE);
}

void WifiStaModeView::formatStatsLine(char* out, size_t out_size) const
{
    if (!out || out_size == 0)
    {
        return;
    }

    const uint32_t pageLoads = WifiManager::webPageLoadCount();
    const uint32_t logins    = WifiManager::webLoginCount();
    const uint32_t saves     = WifiManager::webSaveCount();
    const uint32_t errors    = WifiManager::webErrorCount();
    const uint32_t downloads = WifiManager::webDownloadCount();

#if BUILD_WITH_SECURITY_LEVEL <= 0
    switch (statsIndex_ % 4)
    {
    case 0:
        snprintf(out, out_size, "page loads: %lu", static_cast<unsigned long>(pageLoads));
        break;
    case 1:
        snprintf(out, out_size, "saves: %lu", static_cast<unsigned long>(saves));
        break;
    case 2:
        snprintf(out, out_size, "errors: %lu", static_cast<unsigned long>(errors));
        break;
    default:
        snprintf(out, out_size, "downloads: %lu", static_cast<unsigned long>(downloads));
        break;
    }
#else
    switch (statsIndex_ % 5)
    {
    case 0:
        snprintf(out, out_size, "page loads: %lu", static_cast<unsigned long>(pageLoads));
        break;
    case 1:
        snprintf(out, out_size, "logins: %lu", static_cast<unsigned long>(logins));
        break;
    case 2:
        snprintf(out, out_size, "saves: %lu", static_cast<unsigned long>(saves));
        break;
    case 3:
        snprintf(out, out_size, "errors: %lu", static_cast<unsigned long>(errors));
        break;
    default:
        snprintf(out, out_size, "downloads: %lu", static_cast<unsigned long>(downloads));
        break;
    }
#endif
}

namespace
{

// -----------------------------------------------------------------------------
// Drawing Helpers
// -----------------------------------------------------------------------------

void draw_fit_line(const char* text, int16_t x, int16_t y, int16_t width, uint8_t font, uint16_t color)
{
    set_text_style(font, color);

    char line[96] = {};
    strlcpy(line, text ? text : "", sizeof(line));

    if (thefly_display.textWidth(line) <= width)
    {
        thefly_display.drawString(line, x, y);
        return;
    }

    size_t len = strlen(line);
    while (len > 3)
    {
        line[len - 3] = '.';
        line[len - 2] = '.';
        line[len - 1] = '.';
        line[len]     = '\0';
        if (thefly_display.textWidth(line) <= width)
        {
            thefly_display.drawString(line, x, y);
            return;
        }
        line[len - 3] = '\0';
        --len;
    }

    thefly_display.drawString("...", x, y);
}

void set_text_style(uint8_t font, uint16_t color)
{
    thefly_display.setTextFont(font);
    thefly_display.setTextSize(1.0f);
    thefly_display.setTextDatum(top_left);
    thefly_display.setTextColor(color, TFT_BLACK);
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

const wifi_item_t* current_station()
{
    const wifi_item_t* connected = WifiManager::connectedWifi();
    return connected ? connected : WifiManager::activeWifi();
}

} // namespace
