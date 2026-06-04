#include "SplashView.h"

#include "Aegis.h"
#include "DiskStats.h"
#include "HapticsWrapper.h"
#include "SpriteDraw.h"
#include "WifiManager.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "sprites.h"
#include "thefly_version.h"
#include "utilfuncs.h"

#include <stdio.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

static constexpr int16_t kMode1TextX      = 144;
static constexpr int16_t kMode2TextX      = 4;
static constexpr int16_t kTextY           = 20;
static constexpr int16_t kTextLineHeight  = 17;
static constexpr float   kTextSize        = 1.0f;
static constexpr uint8_t kTextFont        = 2;
static constexpr int16_t kBluetoothY      = 60;
static constexpr int16_t kBtNameY         = 77;
static constexpr int16_t kBdaddrY         = 94;
static constexpr int16_t kWifiLabelY      = 116;
static constexpr int16_t kWifiMacY        = 133;
static constexpr int16_t kExtraInfoY      = 154;

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

struct SplashSprite
{
    const uint8_t* data     = nullptr;
    size_t         bytes    = 0;
    uint32_t       width    = 0;
    uint32_t       height   = 0;
    int16_t        textX    = kMode1TextX;
};

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

extern FlyGui* gui;

static uint8_t g_splash_mode = 1;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

void draw_splash_boot_info();

static void         choose_splash_mode();
static SplashSprite current_splash_sprite();
static void         draw_splash_mac_info(int16_t textX);
static void         draw_splash_optional_boot_info(int16_t textX);
static void         draw_splash_text_line(const char* text, int16_t textX, int16_t y);
static void         draw_splash_tamper_code();
static void         truncate_to_width(char* text, size_t text_size, int16_t width);
static void         format_mac_hyphen(const uint8_t mac[6], char* out, size_t out_size);
static void         format_default_bt_name(const uint8_t mac[6], char* out, size_t out_size);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

SplashView::SplashView() : FlyGuiView(FLYGUI_VIEW_SPLASH) {}

void SplashView::onLoad()
{
    choose_splash_mode();
    FlyGuiView::onLoad();
}

void SplashView::onUnload()
{
    FlyGuiView::onUnload();
}

bool SplashView::handleTouch(const FlyGuiTouchEvent& event)
{
    if (event.justPressed || event.justReleased)
    {
        haptic_play_click();
        if (gui())
        {
            gui()->showView(FLYGUI_VIEW_MAIN);
        }
    }

    return true;
}

bool SplashView::redraw(bool forced)
{
    if (!forced && !dirty())
    {
        return false;
    }

    const SplashSprite           splash = current_splash_sprite();
    const SpriteDraw::DrawResult result =
        SpriteDraw::drawPng(splash.data, splash.bytes, 0, 0, splash.width, splash.height, true);
    if (!result.ok)
    {
        thefly_display.fillScreen(TFT_BLACK);
    }

    #ifdef SPLASH_SHOW_SPECIAL_ICON
    #if SPLASH_SHOW_SPECIAL_ICON == 1
    if (g_splash_mode == 2)
    {
        SpriteDraw::drawPng(sprite_overlay_cat_50,
                            SPRITE_OVERLAY_CAT_50_BYTES,
                            0,
                            static_cast<int16_t>(thefly_display.height() - SPRITE_OVERLAY_CAT_50_HEIGHT),
                            SPRITE_OVERLAY_CAT_50_WIDTH,
                            SPRITE_OVERLAY_CAT_50_HEIGHT,
                            true);
    }
    else
    {
        SpriteDraw::drawPng(sprite_overlay_cat_50,
                            SPRITE_OVERLAY_CAT_50_BYTES,
                            static_cast<int16_t>(thefly_display.width() - SPRITE_OVERLAY_CAT_50_WIDTH),
                            static_cast<int16_t>(thefly_display.height() - SPRITE_OVERLAY_CAT_50_HEIGHT),
                            SPRITE_OVERLAY_CAT_50_WIDTH,
                            SPRITE_OVERLAY_CAT_50_HEIGHT,
                            true);
    }
    #elif SPLASH_SHOW_SPECIAL_ICON == 2
    if (g_splash_mode == 2)
    {
        SpriteDraw::drawPng(sprite_overlay_dog_50,
                            SPRITE_OVERLAY_DOG_50_BYTES,
                            0,
                            static_cast<int16_t>(thefly_display.height() - SPRITE_OVERLAY_DOG_50_HEIGHT),
                            SPRITE_OVERLAY_DOG_50_WIDTH,
                            SPRITE_OVERLAY_DOG_50_HEIGHT,
                            true);
    }
    else
    {
        SpriteDraw::drawPng(sprite_overlay_dog_50,
                            SPRITE_OVERLAY_DOG_50_BYTES,
                            static_cast<int16_t>(thefly_display.width() - SPRITE_OVERLAY_DOG_50_WIDTH),
                            static_cast<int16_t>(thefly_display.height() - SPRITE_OVERLAY_DOG_50_HEIGHT),
                            SPRITE_OVERLAY_DOG_50_WIDTH,
                            SPRITE_OVERLAY_DOG_50_HEIGHT,
                            true);
    }
    #endif
    #endif

    markClean();
    return true;
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

void draw_splash_boot_info()
{
    const SplashSprite splash = current_splash_sprite();

    char text[80];
    snprintf(text,
             sizeof(text),
             "FW: %s\nSecurity: %d",
             version_str ? version_str : "unknown",
             BUILD_WITH_SECURITY_LEVEL);

    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextDatum(top_left);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);

    int16_t y = kTextY;
    for (char* line = text; line && *line; y = static_cast<int16_t>(y + kTextLineHeight))
    {
        char* newline = strchr(line, '\n');
        if (newline)
        {
            *newline = '\0';
        }

        thefly_display.drawString(line, splash.textX, y);
        line = newline ? newline + 1 : nullptr;
    }

    draw_splash_mac_info(splash.textX);
    draw_splash_optional_boot_info(splash.textX);
    draw_splash_tamper_code();

    if (gui)
    {
        gui->requestTopBarFullRedraw();
        gui->redraw(false);
    }
}

static void draw_splash_mac_info(int16_t textX)
{
    uint8_t bdaddr[6]       = {};
    uint8_t wifi_mac[6]     = {};
    char    bt_name[32]     = "The Fly";
    char    bdaddr_text[18] = "unknown";
    char    wifi_text[18]   = "unknown";
    if (esp_read_mac(bdaddr, ESP_MAC_BT) == ESP_OK)
    {
        format_default_bt_name(bdaddr, bt_name, sizeof(bt_name));
        format_mac_hyphen(bdaddr, bdaddr_text, sizeof(bdaddr_text));
    }
    if (esp_read_mac(wifi_mac, ESP_MAC_WIFI_STA) == ESP_OK)
    {
        format_mac_hyphen(wifi_mac, wifi_text, sizeof(wifi_text));
    }

    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextDatum(top_left);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);

    thefly_display.drawString("Bluetooth:", textX, kBluetoothY);
    thefly_display.drawString(bt_name, textX, kBtNameY);
    thefly_display.drawString(bdaddr_text, textX, kBdaddrY);
    thefly_display.drawString("Wi-Fi MAC:", textX, kWifiLabelY);
    thefly_display.drawString(wifi_text, textX, kWifiMacY);
}

static void draw_splash_optional_boot_info(int16_t textX)
{
    uint64_t totalBytes = 0;
    uint64_t freeBytes  = 0;
    if (!DiskStats::diskSpace(totalBytes, freeBytes) || totalBytes == 0)
    {
        return;
    }

    char    freeText[12]  = {};
    char    totalText[12] = {};
    char    line[80]      = {};
    int16_t y             = kExtraInfoY;

    format_bytes(freeBytes, freeText, sizeof(freeText));
    format_bytes(totalBytes, totalText, sizeof(totalText));
    snprintf(line, sizeof(line), "Free: %s / %s", freeText, totalText);
    draw_splash_text_line(line, textX, y);
    y = static_cast<int16_t>(y + kTextLineHeight);

    const char* timezone = WifiManager::timezone();
    if (WifiManager::lastLoadResult() == WifiManager::LoadResult::Ok && timezone && timezone[0] != '\0')
    {
        snprintf(line, sizeof(line), "TZ: %s", timezone);
        draw_splash_text_line(line, textX, y);
    }
}

static void draw_splash_text_line(const char* text, int16_t textX, int16_t y)
{
    if (!text || text[0] == '\0')
    {
        return;
    }

    char line[80] = {};
    strlcpy(line, text, sizeof(line));

    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextDatum(top_left);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);

    const int16_t width = static_cast<int16_t>(thefly_display.width() - textX - 4);
    truncate_to_width(line, sizeof(line), width);
    thefly_display.drawString(line, textX, y);
}

static void draw_splash_tamper_code()
{
#if BUILD_WITH_SECURITY_LEVEL == 2
    uint32_t code = 0;
    if (!Aegis::tamperEvidenceCode(code))
    {
        return;
    }

    char text[5] = {};
    snprintf(text, sizeof(text), "%04lX", static_cast<unsigned long>((code >> 16) & 0xFFFF));

    thefly_display.setTextFont(4);
    thefly_display.setTextSize(1.0f);
    thefly_display.setTextDatum(bottom_right);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);
    thefly_display.drawString(text,
                              static_cast<int16_t>(thefly_display.width() - 4),
                              static_cast<int16_t>(thefly_display.height() - 4));
#endif
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static void choose_splash_mode()
{
    g_splash_mode = (esp_random() & 0x01) == 0 ? 1 : 2;
}

static SplashSprite current_splash_sprite()
{
    if (g_splash_mode == 2)
    {
        return {sprite_splash2, SPRITE_SPLASH2_BYTES, SPRITE_SPLASH2_WIDTH, SPRITE_SPLASH2_HEIGHT, kMode2TextX};
    }

    return {sprite_splash, SPRITE_SPLASH_BYTES, SPRITE_SPLASH_WIDTH, SPRITE_SPLASH_HEIGHT, kMode1TextX};
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

static void truncate_to_width(char* text, size_t text_size, int16_t width)
{
    static constexpr const char* kEllipsis    = "...";
    static constexpr size_t      kEllipsisLen = 3;

    if (!text || text_size <= kEllipsisLen + 1 || width <= 0 || thefly_display.textWidth(text) <= width)
    {
        return;
    }

    size_t len = strlen(text);
    while (len > 0)
    {
        --len;
        text[len] = '\0';
        strncat(text, kEllipsis, text_size - strlen(text) - 1);
        if (thefly_display.textWidth(text) <= width)
        {
            return;
        }
        text[len] = '\0';
    }

    strlcpy(text, kEllipsis, text_size);
}

static void format_mac_hyphen(const uint8_t mac[6], char* out, size_t out_size)
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

static void format_default_bt_name(const uint8_t mac[6], char* out, size_t out_size)
{
    if (!out || out_size == 0)
    {
        return;
    }

    if (!mac)
    {
        strlcpy(out, "The Fly", out_size);
        return;
    }

    snprintf(out, out_size, "The Fly %02X%02X", mac[4], mac[5]);
}
