#include "WifiApModeView.h"

#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "Display.h"
#include "HapticsWrapper.h"
#include "QrCodeView.h"
#include "SpriteDraw.h"
#include "WifiManager.h"
#include "sprites.h"
extern QrCodeView* get_view_qr_code();

namespace
{
constexpr int16_t     kContentY                  = FlyGui::kTopBarHeight;
constexpr int16_t     kWifiIconX                 = 4;
constexpr int16_t     kWifiIconY                 = FlyGui::kTopBarHeight + 2;
constexpr int16_t     kSecurityIconX             = 54;
constexpr int16_t     kEyeSize                   = 100;
constexpr int16_t     kEyeX                      = 220;
constexpr int16_t     kEyeY                      = 140;
constexpr int16_t     kClientInfoX               = 108;
constexpr int16_t     kClientInfoY               = FlyGui::kTopBarHeight + 2;
constexpr int16_t     kClientInfoWidth           = 212;
constexpr int16_t     kClientInfoLineHeight      = 16;
constexpr int16_t     kTextX                     = 8;
constexpr int16_t     kTextY                     = 68;
constexpr int16_t     kTextWidth                 = 206;
constexpr int16_t     kCredentialLabelLineHeight = 18;
constexpr int16_t     kCredentialValueLineHeight = 28;
constexpr int16_t     kStatsY                    = 190;
constexpr int16_t     kSmallLineHeight           = 10;
constexpr uint8_t     kNormalTextFont            = 2;
constexpr uint8_t     kClientInfoTextFont        = kNormalTextFont;
constexpr uint8_t     kCredentialValueTextFont   = 4;
constexpr uint8_t     kSmallTextFont             = 1;
constexpr uint32_t    kClientInfoDrawMs          = 500;
constexpr uint32_t    kStatsCycleMs              = 3000;
constexpr uint32_t    kQrHoldMs                  = 5000;
constexpr const char* kHiddenText                = "**********";
constexpr const char* kExitHint                  = "power-off/reset to stop";

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void draw_fit_line(const char* text, int16_t x, int16_t y, int16_t width, uint8_t font, uint16_t color);
static void set_text_style(uint8_t font, uint16_t color);
static bool append_wifi_qr_text(char* out, size_t out_size, size_t& used, const char* text);
static bool append_text(char* out, size_t out_size, size_t& used, const char* text);
static bool append_char(char* out, size_t out_size, size_t& used, char value);
static void format_mac_hyphen(const uint8_t mac[6], char* out, size_t out_size);
} // namespace

WifiApModeView* WifiApModeView::activeView_ = nullptr;

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

WifiApModeView::WifiApModeView()
    : FlyGuiView(FLYGUI_VIEW_AP_MODE), wifiIcon_(kWifiIconX, kWifiIconY, SPRITE_WIFI_50_WIDTH, SPRITE_WIFI_50_HEIGHT),
      securityIcon_(kSecurityIconX, kWifiIconY, SPRITE_SECURITY_50_WIDTH, SPRITE_SECURITY_50_HEIGHT),
      eyeItem_(kEyeX, kEyeY, kEyeSize, kEyeSize)
{
    wifiIcon_.setSprite(sprite_wifi_50, SPRITE_WIFI_50_WIDTH, SPRITE_WIFI_50_HEIGHT, SPRITE_WIFI_50_BYTES);
    addItem(wifiIcon_);

    securityIcon_.setSprite(sprite_security_50,
                            SPRITE_SECURITY_50_WIDTH,
                            SPRITE_SECURITY_50_HEIGHT,
                            SPRITE_SECURITY_50_BYTES);
    addItem(securityIcon_);

    syncEyeSprite();
    addItem(eyeItem_);
}

void WifiApModeView::onLoad()
{
    activeView_       = this;
    showSensitive_    = true;
    statsIndex_       = 0;
    lastClientDrawMs_ = 0;
    lastStatsDrawMs_  = 0;
    resetQrHold();
    syncEyeSprite();
    FlyGuiView::onLoad();
}

void WifiApModeView::onUnload()
{
    if (activeView_ == this)
    {
        activeView_ = nullptr;
    }
    resetQrHold();
    FlyGuiView::onUnload();
}

bool WifiApModeView::handleTouch(const FlyGuiTouchEvent& event)
{
    const bool eyeEvent = eyeItem_.contains(event.x, event.y) || qrHoldActive_;
    if (!eyeEvent)
    {
        return FlyGuiView::handleTouch(event);
    }

    if (event.justPressed && eyeItem_.contains(event.x, event.y))
    {
        qrHoldActive_    = true;
        qrHoldStartedMs_ = millis();
        qrHoldFadeDrawn_ = false;
        return true;
    }

    if (!qrHoldActive_)
    {
        return true;
    }

    const uint32_t held_ms = millis() - qrHoldStartedMs_;
    if (event.pressed && held_ms >= kQrHoldMs && !qrHoldFadeDrawn_)
    {
        haptic_play_click();
        drawQrHoldFade();
    }

    if (event.justReleased)
    {
        const bool show_qr         = held_ms >= kQrHoldMs;
        const bool released_on_eye = eyeItem_.contains(event.x, event.y);
        resetQrHold();
        if (show_qr)
        {
            showQrCode();
        }
        else if (released_on_eye)
        {
            toggleSensitive();
        }
    }

    return true;
}

bool WifiApModeView::redraw(bool forced)
{
    if (qrHoldFadeDrawn_)
    {
        return false;
    }

    const bool redrawStatic = forced || dirty();
    if (redrawStatic)
    {
        drawStaticContent();
        bool drawn = true;
        drawn |= drawClientInfo(true);
        drawn |= drawStatsLine(true);
        markClean();
        return drawn;
    }

    bool drawn = drawClientInfo(false);
    drawn |= drawStatsLine(false);
    return drawn;
}

void WifiApModeView::onPressRight()
{
    toggleSensitive();
}

void WifiApModeView::toggleSensitive()
{
    haptic_play_click();
    showSensitive_ = !showSensitive_;
    syncEyeSprite();
    setDirty();
}

void WifiApModeView::resetQrHold()
{
    qrHoldStartedMs_ = 0;
    qrHoldActive_    = false;
    qrHoldFadeDrawn_ = false;
}

void WifiApModeView::drawQrHoldFade()
{
    for (int16_t y = FlyGui::kTopBarHeight; y < thefly_display.height(); y += 3)
    {
        thefly_display.drawFastHLine(0, y, thefly_display.width(), TFT_WHITE);
        thefly_display.drawFastHLine(0, y + 1, thefly_display.width(), TFT_WHITE);
    }
    qrHoldFadeDrawn_ = true;
    markClean();
}

void WifiApModeView::showQrCode()
{
    QrCodeView* qr_view      = get_view_qr_code();
    FlyGui*     owner        = gui();
    char        qr_text[384] = {};
    if (!qr_view || !owner || !makeWifiQrText(qr_text, sizeof(qr_text)))
    {
        setDirty();
        return;
    }

    qr_view->configure(qr_text);
    owner->showView(FLYGUI_VIEW_QR_CODE);
}

void WifiApModeView::syncEyeSprite()
{
    if (showSensitive_)
    {
        eyeItem_.setSprite(sprite_eye_open_100,
                           SPRITE_EYE_OPEN_100_WIDTH,
                           SPRITE_EYE_OPEN_100_HEIGHT,
                           SPRITE_EYE_OPEN_100_BYTES);
        return;
    }

    eyeItem_.setSprite(sprite_eye_closed_100,
                       SPRITE_EYE_CLOSED_100_WIDTH,
                       SPRITE_EYE_CLOSED_100_HEIGHT,
                       SPRITE_EYE_CLOSED_100_BYTES);
}

void WifiApModeView::drawStaticContent()
{
    thefly_display.fillRect(0,
                            kContentY,
                            thefly_display.width(),
                            static_cast<int16_t>(thefly_display.height() - kContentY),
                            TFT_BLACK);

    securityIcon_.setVisible(WifiManager::isGeneratedSoftApActive());
    wifiIcon_.redraw(true);
    securityIcon_.redraw(true);
    eyeItem_.redraw(true);
    drawCredentials();
}

bool WifiApModeView::drawClientInfo(bool forced)
{
    const uint32_t now = millis();
    if (!forced && now - lastClientDrawMs_ < kClientInfoDrawMs)
    {
        return false;
    }
    lastClientDrawMs_ = now;

    const bool secureAp = WifiManager::isGeneratedSoftApActive();

    char    client_text[24] = "To: nobody";
    uint8_t client_mac[6]   = {};
    if (WifiManager::softApClientMac(client_mac))
    {
        char mac_text[18] = {};
        format_mac_hyphen(client_mac, mac_text, sizeof(mac_text));
        snprintf(client_text, sizeof(client_text), "TO: %s", mac_text);
    }

    const uint32_t connectionCount = WifiManager::softApClientConnectionCount();
    char           count_text[24]  = {};
    snprintf(count_text,
             sizeof(count_text),
             "%lux %s",
             static_cast<unsigned long>(connectionCount),
             connectionCount > 1 ? "reconnections" : "connections");

    thefly_display.fillRect(kClientInfoX,
                            kClientInfoY,
                            kClientInfoWidth,
                            static_cast<int16_t>(kClientInfoLineHeight * 3),
                            TFT_BLACK);
    draw_fit_line(secureAp ? "Secure AP Mode" : "AP mode",
                  kClientInfoX,
                  kClientInfoY,
                  kClientInfoWidth,
                  kClientInfoTextFont,
                  TFT_WHITE);
    draw_fit_line(client_text,
                  kClientInfoX,
                  static_cast<int16_t>(kClientInfoY + kClientInfoLineHeight),
                  kClientInfoWidth,
                  kClientInfoTextFont,
                  TFT_WHITE);
    draw_fit_line(count_text,
                  kClientInfoX,
                  static_cast<int16_t>(kClientInfoY + (kClientInfoLineHeight * 2)),
                  kClientInfoWidth,
                  kClientInfoTextFont,
                  TFT_WHITE);
    return true;
}

void WifiApModeView::drawCredentials()
{
    const wifi_item_t* active   = WifiManager::activeWifi();
    const char*        ssid     = active && active->ssid[0] != '\0' ? active->ssid : "";
    const char*        password = WifiManager::softApPassword();
    const String       ipText   = WifiManager::softApIp().toString();

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

    const int16_t hintY = static_cast<int16_t>(thefly_display.height() - kSmallLineHeight);
    thefly_display.fillRect(kTextX, hintY, kTextWidth, kSmallLineHeight, TFT_BLACK);
    draw_fit_line(kExitHint, kTextX, hintY, kTextWidth, kSmallTextFont, TFT_RED);
}

bool WifiApModeView::drawStatsLine(bool forced)
{
    const uint32_t now = millis();
#if BUILD_WITH_SECURITY_LEVEL <= 0
    constexpr uint8_t kStatsCount = 4;
#else
    constexpr uint8_t kStatsCount = 5;
#endif

    if (!forced && now - lastStatsDrawMs_ < kStatsCycleMs)
    {
        return false;
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
    return true;
}

void WifiApModeView::formatStatsLine(char* out, size_t out_size) const
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

bool WifiApModeView::makeWifiQrText(char* out, size_t out_size) const
{
    if (!out || out_size == 0)
    {
        return false;
    }

    out[0]                      = '\0';
    const wifi_item_t* active   = WifiManager::activeWifi();
    const char*        ssid     = active && active->ssid[0] != '\0' ? active->ssid : nullptr;
    const char*        password = WifiManager::softApPassword();
    if (!password && active && active->password[0] != '\0')
    {
        password = active->password;
    }
    if (!ssid || ssid[0] == '\0')
    {
        return false;
    }

    size_t used = 0;
    if (!append_text(out, out_size, used, password && password[0] != '\0' ? "WIFI:T:WPA;S:" : "WIFI:T:nopass;S:") ||
        !append_wifi_qr_text(out, out_size, used, ssid))
    {
        return false;
    }

    if (password && password[0] != '\0' &&
        (!append_text(out, out_size, used, ";P:") || !append_wifi_qr_text(out, out_size, used, password)))
    {
        return false;
    }

    return append_text(out, out_size, used, ";H:false;;");
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
// Formatting Helpers
// -----------------------------------------------------------------------------

bool append_wifi_qr_text(char* out, size_t out_size, size_t& used, const char* text)
{
    for (const char* cursor = text ? text : ""; *cursor; ++cursor)
    {
        const char value = *cursor;
        if ((value == '\\' || value == ';' || value == ',' || value == ':' || value == '"') &&
            !append_char(out, out_size, used, '\\'))
        {
            return false;
        }
        if (!append_char(out, out_size, used, value))
        {
            return false;
        }
    }
    return true;
}

bool append_text(char* out, size_t out_size, size_t& used, const char* text)
{
    for (const char* cursor = text ? text : ""; *cursor; ++cursor)
    {
        if (!append_char(out, out_size, used, *cursor))
        {
            return false;
        }
    }
    return true;
}

bool append_char(char* out, size_t out_size, size_t& used, char value)
{
    if (!out || used + 1 >= out_size)
    {
        return false;
    }

    out[used++] = value;
    out[used]   = '\0';
    return true;
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

    snprintf(out, out_size, "%02X-%02X-%02X-%02X-%02X-%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

} // namespace
