#include "WifiApModeView.h"

#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "Display.h"
#include "SpriteDraw.h"
#include "WifiManager.h"
#include "sprites.h"

extern WifiManager* wifi_manager;

namespace
{
constexpr int16_t  kContentY         = FlyGui::kTopBarHeight;
constexpr int16_t  kWifiIconX        = 4;
constexpr int16_t  kWifiIconY        = FlyGui::kTopBarHeight + 2;
constexpr int16_t  kSecurityIconX    = 54;
constexpr int16_t  kEyeSize          = 100;
constexpr int16_t  kEyeX             = 220;
constexpr int16_t  kEyeY             = 140;
constexpr int16_t  kClientInfoX      = 104;
constexpr int16_t  kClientInfoY      = FlyGui::kTopBarHeight + 2;
constexpr int16_t  kClientInfoWidth  = 212;
constexpr int16_t  kTextX            = 8;
constexpr int16_t  kTextY            = 68;
constexpr int16_t  kTextWidth        = 206;
constexpr int16_t  kCredentialLabelLineHeight = 18;
constexpr int16_t  kCredentialValueLineHeight = 28;
constexpr int16_t  kStatsY           = 190;
constexpr int16_t  kHintY            = 204;
constexpr int16_t  kSmallLineHeight  = 10;
constexpr uint8_t  kNormalTextFont   = 2;
constexpr uint8_t  kCredentialValueTextFont = 4;
constexpr uint8_t  kSmallTextFont    = 1;
constexpr uint32_t kClientInfoDrawMs = 500;
constexpr uint32_t kStatsCycleMs     = 3000;
constexpr const char* kHiddenText     = "**********";
constexpr const char* kExitHint       = "power-off/reset to stop";

void set_text_style(uint8_t font, uint16_t color)
{
    thefly_display.setTextFont(font);
    thefly_display.setTextSize(1.0f);
    thefly_display.setTextDatum(top_left);
    thefly_display.setTextColor(color, TFT_BLACK);
}

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
        line[len] = '\0';
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

void format_mac_hyphen(const uint8_t mac[6], char* out, size_t out_size)
{
    if (!out || out_size == 0)
    {
        return;
    }
    if (!mac)
    {
        out[0] = '\0';
        return;
    }

    snprintf(out,
             out_size,
             "%02X-%02X-%02X-%02X-%02X-%02X",
             mac[0],
             mac[1],
             mac[2],
             mac[3],
             mac[4],
             mac[5]);
}
} // namespace

WifiApModeView* WifiApModeView::activeView_ = nullptr;

WifiApModeView::WifiApModeView()
    : FlyGuiView(FLYGUI_VIEW_AP_MODE),
      wifiIcon_(kWifiIconX, kWifiIconY, SPRIT_WIFI_50_WIDTH, SPRIT_WIFI_50_HEIGHT),
      securityIcon_(kSecurityIconX, kWifiIconY, SPRIT_SECURITY_50_WIDTH, SPRIT_SECURITY_50_HEIGHT),
      eyeItem_(kEyeX, kEyeY, kEyeSize, kEyeSize)
{
    wifiIcon_.setSprite(sprit_wifi_50, SPRIT_WIFI_50_WIDTH, SPRIT_WIFI_50_HEIGHT, SPRIT_WIFI_50_BYTES);
    addItem(wifiIcon_);

    securityIcon_.setSprite(sprit_security_50, SPRIT_SECURITY_50_WIDTH, SPRIT_SECURITY_50_HEIGHT, SPRIT_SECURITY_50_BYTES);
    addItem(securityIcon_);

    eyeItem_.setCallback(onEyeTriggered);
    syncEyeSprite();
    addItem(eyeItem_);
}

void WifiApModeView::onLoad()
{
    activeView_ = this;
    showSensitive_ = false;
    statsIndex_ = 0;
    lastClientDrawMs_ = 0;
    lastStatsDrawMs_ = 0;
    syncEyeSprite();
    FlyGuiView::onLoad();
}

void WifiApModeView::onUnload()
{
    if (activeView_ == this)
    {
        activeView_ = nullptr;
    }
    FlyGuiView::onUnload();
}

void WifiApModeView::redraw(bool forced)
{
    const bool redrawStatic = forced || dirty();
    if (redrawStatic)
    {
        drawStaticContent();
        drawClientInfo(true);
        drawStatsLine(true);
        markClean();
        return;
    }

    drawClientInfo(false);
    drawStatsLine(false);
}

void WifiApModeView::onPressRight()
{
    toggleSensitive();
}

void WifiApModeView::onEyeTriggered(uint32_t)
{
    if (activeView_)
    {
        activeView_->toggleSensitive();
    }
}

void WifiApModeView::toggleSensitive()
{
    showSensitive_ = !showSensitive_;
    syncEyeSprite();
    setDirty();
}

void WifiApModeView::syncEyeSprite()
{
    if (showSensitive_)
    {
        eyeItem_.setSprite(sprit_eye_open_100, SPRIT_EYE_OPEN_100_WIDTH, SPRIT_EYE_OPEN_100_HEIGHT, SPRIT_EYE_OPEN_100_BYTES);
        return;
    }

    eyeItem_.setSprite(sprit_eye_closed_100, SPRIT_EYE_CLOSED_100_WIDTH, SPRIT_EYE_CLOSED_100_HEIGHT, SPRIT_EYE_CLOSED_100_BYTES);
}

void WifiApModeView::drawStaticContent()
{
    thefly_display.fillRect(0,
                            kContentY,
                            thefly_display.width(),
                            static_cast<int16_t>(thefly_display.height() - kContentY),
                            TFT_BLACK);

    securityIcon_.setVisible(wifi_manager && wifi_manager->isGeneratedSoftApActive());
    wifiIcon_.redraw(true);
    securityIcon_.redraw(true);
    eyeItem_.redraw(true);
    drawCredentials();
}

void WifiApModeView::drawClientInfo(bool forced)
{
    const uint32_t now = millis();
    if (!forced && now - lastClientDrawMs_ < kClientInfoDrawMs)
    {
        return;
    }
    lastClientDrawMs_ = now;

    char client_text[18] = "nobody";
    uint8_t client_mac[6] = {};
    if (wifi_manager && wifi_manager->softApClientMac(client_mac))
    {
        format_mac_hyphen(client_mac, client_text, sizeof(client_text));
    }

    char count_text[16] = {};
    snprintf(count_text,
             sizeof(count_text),
             "%lu",
             static_cast<unsigned long>(wifi_manager ? wifi_manager->softApClientConnectionCount() : 0));

    thefly_display.fillRect(kClientInfoX,
                            kClientInfoY,
                            kClientInfoWidth,
                            static_cast<int16_t>(kSmallLineHeight * 2),
                            TFT_BLACK);
    draw_fit_line(client_text, kClientInfoX, kClientInfoY, kClientInfoWidth, kSmallTextFont, TFT_WHITE);
    draw_fit_line(count_text,
                  kClientInfoX,
                  static_cast<int16_t>(kClientInfoY + kSmallLineHeight),
                  kClientInfoWidth,
                  kSmallTextFont,
                  TFT_WHITE);
}

void WifiApModeView::drawCredentials()
{
    const wifi_item_t* active = wifi_manager ? wifi_manager->activeWifi() : nullptr;
    const char* ssid = active && active->ssid[0] != '\0' ? active->ssid : "";
    const char* password = wifi_manager ? wifi_manager->softApPassword() : nullptr;
    const String ipText = WiFi.softAPIP().toString();

    thefly_display.fillRect(kTextX, kTextY, kTextWidth, kStatsY - kTextY, TFT_BLACK);

    int16_t y = kTextY;
    draw_fit_line("SSID:", kTextX, y, kTextWidth, kNormalTextFont, TFT_WHITE);
    y = static_cast<int16_t>(y + kCredentialLabelLineHeight);
    draw_fit_line(showSensitive_ ? ssid : kHiddenText, kTextX, y, kTextWidth, kCredentialValueTextFont, TFT_YELLOW);
    y = static_cast<int16_t>(y + kCredentialValueLineHeight);
    draw_fit_line("Password:", kTextX, y, kTextWidth, kNormalTextFont, TFT_WHITE);
    y = static_cast<int16_t>(y + kCredentialLabelLineHeight);
    draw_fit_line(showSensitive_ && password ? password : kHiddenText,
                  kTextX,
                  y,
                  kTextWidth,
                  kCredentialValueTextFont,
                  TFT_YELLOW);
    y = static_cast<int16_t>(y + kCredentialValueLineHeight);

    char ipLine[48] = {};
    snprintf(ipLine, sizeof(ipLine), "IP: %s", ipText.c_str());
    draw_fit_line(ipLine, kTextX, y, kTextWidth, kNormalTextFont, TFT_WHITE);

    thefly_display.fillRect(kTextX, kHintY, kTextWidth, kSmallLineHeight, TFT_BLACK);
    draw_fit_line(kExitHint, kTextX, kHintY, kTextWidth, kSmallTextFont, TFT_RED);
}

void WifiApModeView::drawStatsLine(bool forced)
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

void WifiApModeView::formatStatsLine(char* out, size_t out_size) const
{
    if (!out || out_size == 0)
    {
        return;
    }

    const uint32_t pageLoads = wifi_manager ? wifi_manager->webPageLoadCount() : 0;
    const uint32_t logins = wifi_manager ? wifi_manager->webLoginCount() : 0;
    const uint32_t saves = wifi_manager ? wifi_manager->webSaveCount() : 0;
    const uint32_t errors = wifi_manager ? wifi_manager->webErrorCount() : 0;
    const uint32_t downloads = wifi_manager ? wifi_manager->webDownloadCount() : 0;

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
