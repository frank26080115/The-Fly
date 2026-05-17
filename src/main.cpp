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
#include "MicroSdCard.h"
#include "WifiManager.h"
#include "esp_log.h"
#include "utilfuncs.h"
#include "all_tests.h"

constexpr const char* MAINTAG = "main.cpp";

extern void all_init();
extern void show_splash();

TaskHandle_t loopTask_core0_Handle = NULL;
static void  loopTask_core0(void* pvParameters);

FlyGui* gui;
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
        ESP_LOGE(MAINTAG, "microSD init failed");
    }

    bt_host_list = new BtHostList();
    if (!bt_host_list->loadFromMicroSd())
    {
        ESP_LOGE(MAINTAG, "BtHostList file load failed");
    }

    wifi_manager = new WifiManager();
    if (!wifi_manager->loadFromMicroSd())
    {
        ESP_LOGE(MAINTAG, "WifiManager file load failed: %s", wifi_manager->lastResultName());
    }

    if (!AudioManager::init())
    {
        ESP_LOGE(MAINTAG, "AudioManager init failed");
    }

    if (!BtManager::init())
    {
        ESP_LOGE(MAINTAG, "BluetoothManager init failed");
    }

    BattTracker::init();

    xTaskCreateUniversal(loopTask_core0, "loopTask_core0", getArduinoLoopTaskStackSize(), NULL, 1, &loopTask_core0_Handle, 0);
}

void loop()
{
    // this is running on core 1
    AudioFileRecorder::pump();
    if (wifi_manager)
    {
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
        taskYIELD();
    }
}
