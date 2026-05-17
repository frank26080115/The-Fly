#include "thefly_common.h"
#include <Arduino.h>
#include "nvs_flash.h"
#include "M5Unified.h"
#include "FlyGui.h"
#include "SpriteDraw.h"
#include "sprites.h"

constexpr const char* TAG = "all_init.cpp";

RTC_DATA_ATTR uint32_t reset_flag  = 0;
RTC_DATA_ATTR uint32_t reset_magic = 0;
bool reset_was_magic = false;

extern FlyGui* gui;

bool init_nvs();
void check_reset_flag();
void init_m5();
void init_gui();

void all_init()
{
    Serial.begin(115200);
    if (!init_nvs())
    {
        ESP_LOGE(TAG, "NVS init failed");
    }
    check_reset_flag();
    init_m5();
    init_gui();
}

bool init_nvs()
{
    // NVS is the ESP-IDF non-volatile key/value store. Bluetooth uses it for
    // controller/host state such as pairing, bonding, and calibration metadata.
    // Initialize it once at boot, before any module tries to bring up Bluetooth.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // The NVS partition can become unreadable after partition-size changes
        // or IDF version changes. Erase it and retry so Bluetooth has a clean
        // store instead of failing later during stack initialization.
        err = nvs_flash_erase();
        if (err != ESP_OK)
        {
            Serial.printf("nvs erase failed: %s\n", esp_err_to_name(err));
            return false;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK)
    {
        Serial.printf("nvs init failed: %s\n", esp_err_to_name(err));
        return false;
    }

    Serial.printf("NVS initialized\n");
    return true;
}

void init_m5()
{
    auto cfg                   = M5.config();
    cfg.internal_spk           = true;
    cfg.internal_mic           = true;
    cfg.external_speaker_value = 0;
    M5.begin(cfg);
    M5.Speaker.end();
    M5.Mic.end();
}

void init_gui()
{
    gui = new FlyGui(M5.Display);
    M5GFX& display = gui->display();
    display.setBrightness(255);
    display.setColorDepth(16);
    display.fillScreen(TFT_BLACK);
}

void show_splash()
{
    M5GFX& display = gui->display();
    const SpriteDraw::DrawResult result =
    SpriteDraw::drawPng(display,
                        sprit_splash,
                        SPRIT_SPLASH_BYTES,
                        0,
                        0,
                        SPRIT_SPLASH_WIDTH,
                        SPRIT_SPLASH_HEIGHT,
                        true,
                        NULL
                    );
    (void) result;
}

void check_reset_flag()
{
    if (reset_magic == RESET_MAGIC)
    {
        ESP_LOGI(TAG, "Booted after flagged reset: flag=%u\n", reset_flag);

        reset_was_magic = true;

        // Clear it so it is one-shot
        reset_magic = 0;
        reset_flag  = 0;
    }
    else
    {
        ESP_LOGI(TAG, "Normal boot / power-on / unflagged reset\n");
    }
}
