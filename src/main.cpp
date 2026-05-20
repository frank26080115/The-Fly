#include "thefly_common.h"
#include <Arduino.h>
#include "M5Unified.h"
#include "nvs_flash.h"
#include "AudioManager.h"
#include "AudioFileRecorder.h"
#include "BattTracker.h"
#include "BluetoothManager.h"
#include "BtHostList.h"
#include "Hotel.h"
#include "FlyGui.h"
#include "Display.h"
#include "MicroSdCard.h"
#include "WifiManager.h"
#include "esp_log.h"
#include "utilfuncs.h"
#include "all_tests.h"

constexpr const char* MAINTAG = "main.cpp";

extern void all_init();
extern void show_splash();
extern bool show_recording_view_bluetooth();
extern bool show_recording_view_memo();

TaskHandle_t loopTask_core0_Handle = NULL;
static void  loopTask_core0(void* pvParameters);

FlyGui* gui;
M5GFX& thefly_display = M5.Display;
BtHostList* bt_host_list;
WifiManager* wifi_manager;

void setup()
{
    #ifdef RUN_BRINGUP_TEST
    run_test();
    #endif

    all_init();

    if (reset_was_magic == false) {
        show_splash();
    }

    if (!MicroSdCard::begin())
    {
        show_fatal_error_f(true, "microSD init failed");
    }

    bt_host_list = new BtHostList();
    if (!bt_host_list->loadFromMicroSd())
    {
        show_fatal_error_f(false, "Bluetooth host list load failed");
    }

    wifi_manager = new WifiManager();
    if (wifi_manager && !wifi_manager->loadFromMicroSd())
    {
        show_fatal_error_f(false, "Wi-Fi configuration load failed: %s", wifi_manager->lastResultName());
    }

    if (!AudioManager::init())
    {
        show_fatal_error_f(true, "AudioManager init failed");
    }

    if (!BtManager::init())
    {
        show_fatal_error_f(true, "BluetoothManager init failed");
    }

    BattTracker::init();

    if (!gui || !gui->showView(FLYGUI_VIEW_MAIN))
    {
        show_fatal_error_f(true, "Failed to show main view");
    }

    xTaskCreateUniversal(loopTask_core0, "loopTask_core0", getArduinoLoopTaskStackSize(), NULL, 1, &loopTask_core0_Handle, 0);
}

void loop()
{
    // this is running on core 1
    gui->poll();
    AudioFileRecorder::pump();
    if (wifi_manager) {
        wifi_manager->poll();
    }
    Hotel::pollCore1();
    taskYIELD();
}

static void loopTask_core0(void* pvParameters)
{
    // this is running on core 0
    while (true)
    {
        AudioManager::pump_task();
        Hotel::pollCore0();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void onclick_main_bluetooth()
{
    ESP_LOGI(MAINTAG, "main screen bluetooth selected");
}

void onclick_main_wifi()
{
    ESP_LOGI(MAINTAG, "main screen wifi selected");
}

void onclick_main_memo()
{
    ESP_LOGI(MAINTAG, "main screen memo selected");
    if (!show_recording_view_memo())
    {
        ESP_LOGE(MAINTAG, "failed to start memo recording view");
    }
}

void onclick_main_smartphone()
{
    ESP_LOGI(MAINTAG, "main screen smartphone selected");
}

void onclick_main_laptop()
{
    ESP_LOGI(MAINTAG, "main screen laptop selected");
}

void onclick_main_wifisearch()
{
    ESP_LOGI(MAINTAG, "main screen wifi search selected");
}

void conn_waiting_cancel()
{
    ESP_LOGI(MAINTAG, "connection waiting cancel selected");
}
