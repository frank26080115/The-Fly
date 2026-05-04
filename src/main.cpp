#include <Arduino.h>
#include "nvs_flash.h"
#include "AudioManager.h"
#include "thefly_common.h"
#include "utilfuncs.h"

constexpr const char* MAINTAG = "main.cpp";

TaskHandle_t loopTask_core0_Handle = NULL;
static void  loopTask_core0(void* pvParameters);

RTC_DATA_ATTR uint32_t reset_flag  = 0;
RTC_DATA_ATTR uint32_t reset_magic = 0;

void setup()
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
        ok(nvs_flash_erase(), "nvs erase");
        err = nvs_flash_init();
    }
    if (!ok(err, "nvs init"))
    {
        return;
    }

    Serial.begin(115200);

    if (reset_magic == RESET_MAGIC)
    {
        ESP_LOGI(MAINTAG, "Booted after flagged reset: flag=%u\n", reset_flag);

        // Clear it so it is one-shot
        reset_magic = 0;
        reset_flag  = 0;
    }
    else
    {
        ESP_LOGI(MAINTAG, "Normal boot / power-on / unflagged reset\n");
    }

    xTaskCreateUniversal(loopTask_core0, "loopTask_core0", getArduinoLoopTaskStackSize(), NULL, 1, &loopTask_core0_Handle, 0);
}

void loop()
{
    // this is running on core 1
}

static void loopTask_core0(void* pvParameters)
{
    // this is running on core 0
    while (true)
    {
        AudioManager::pump_task();
        taskYIELD();
    }
}
