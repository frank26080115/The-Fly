#include "thefly_common.h"
#include <Arduino.h>
#include "M5Unified.h"
#include <WiFi.h>
#include "nvs_flash.h"
#include "AudioManager.h"
#include "AudioFileRecorder.h"
#include "BattTracker.h"
#include "BluetoothManager.h"
#include "BtHostList.h"
#include "DiskStats.h"
#include "Hotel.h"
#include "FlyGui.h"
#include "Display.h"
#include "MicroSdCard.h"
#include "ModalDialog.h"
#include "RecordingView/RecordingViewCallbacks.h"
#include "ScrollView/ScrollView.h"
#include "WifiManager.h"
#include "esp_log.h"
#include "sprites.h"
#include "utilfuncs.h"
#include "all_tests.h"

constexpr const char* MAINTAG = "main.cpp";

extern void all_init();
extern void show_splash();
extern bool show_recording_view_bluetooth();
extern bool show_recording_view_memo();
extern ScrollView* get_scroll_view();
extern ModalDialog* get_modal_dialog();

void onclick_scroll_exit();
void onclick_bluetooth_host(int32_t value);
void onclick_bluetooth_pair(int32_t value);
void onclick_wifi_scan_and_connect(int32_t value);
void onclick_wifi_station(int32_t value);
void onclick_wifi_ap(int32_t value);
void onclick_cloud_upload(int32_t value);
void onclick_ntp_sync(int32_t value);
void onclick_bt_show_info(int32_t value);
void onclick_wifi_show_info(int32_t value);

TaskHandle_t loopTask_core0_Handle = NULL;
static void  loopTask_core0(void* pvParameters);
static bool  show_info_dialog(const char* text, uint16_t next_view);
static bool  bluetooth_recording_state(BtManager::State state);
static void  on_bluetooth_state_changed(BtManager::State state);
static void  handle_pending_bluetooth_recording();
static void  on_wifi_scan_finished(const wifi_item_t* item);

FlyGui* gui;
M5GFX& thefly_display = M5.Display;
BtHostList* bt_host_list;
WifiManager* wifi_manager;
static volatile bool g_pending_bluetooth_recording = false;

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
    if (wifi_manager)
    {
        wifi_manager->setOnScanFinished(on_wifi_scan_finished);
    }

    if (!AudioManager::init())
    {
        show_fatal_error_f(true, "AudioManager init failed");
    }

    BtManager::setStateChangedCallback(on_bluetooth_state_changed);
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
    handle_pending_bluetooth_recording();
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
        BtManager::poll();
        AudioManager::pump_task();
        Hotel::pollCore0();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static bool bluetooth_recording_state(BtManager::State state)
{
    return state == BtManager::State::Connected || state == BtManager::State::AudioAvailable;
}

static void on_bluetooth_state_changed(BtManager::State state)
{
    if (bluetooth_recording_state(state))
    {
        g_pending_bluetooth_recording = true;
    }
}

static void handle_pending_bluetooth_recording()
{
    if (!g_pending_bluetooth_recording)
    {
        return;
    }
    g_pending_bluetooth_recording = false;

    if (!bluetooth_recording_state(BtManager::state()))
    {
        return;
    }

    if (AudioFileRecorder::isRecording())
    {
        return;
    }

    ESP_LOGI(MAINTAG, "Bluetooth host connection detected, starting call recording");
    if (!RecordingViewCallbacks::beginBluetoothRecording('C'))
    {
        ESP_LOGE(MAINTAG, "failed to start Bluetooth recording");
        show_fatal_error_f(false, "Bluetooth recording start failed");
        return;
    }

    if (!show_recording_view_bluetooth())
    {
        ESP_LOGE(MAINTAG, "failed to show Bluetooth recording view");
        RecordingViewCallbacks::stopRecording();
        show_fatal_error_f(false, "Bluetooth recording view failed");
    }
}

void onclick_main_bluetooth()
{
    ESP_LOGI(MAINTAG, "main screen bluetooth selected");
    ScrollView* scroll_view = get_scroll_view();
    if (scroll_view && gui)
    {
        scroll_view->populateBluetooth(bt_host_list);
        gui->showView(FLYGUI_VIEW_SCROLL);
    }
}

void onclick_main_info()
{
    ESP_LOGI(MAINTAG, "main screen info selected");

    // fade the screen really fast as a visual reaction, the analysis calls are slow
    FlyGui::quickScreenFade();

    const bool disk_ok = DiskStats::refreshDiskSpace();
    const bool rec_ok  = DiskStats::refreshRecordingUploadStats();

    char free_text[20];
    char total_text[20];
    format_bytes(DiskStats::freeDiskSpace(), free_text, sizeof(free_text));
    format_bytes(DiskStats::totalDiskSpace(), total_text, sizeof(total_text));

    const char* last_upload = DiskStats::lastUploadDateTime();
    const char* latest_file = DiskStats::latestRecordedFileName();
    if (!last_upload || last_upload[0] == '\0')
    {
        last_upload = "never";
    }
    if (!latest_file || latest_file[0] == '\0')
    {
        latest_file = "none";
    }

    char text[192];
    if (disk_ok && rec_ok)
    {
        snprintf(text,
                 sizeof(text),
                 "Disk: %s/%s free (%u%%)\nFiles: %lu total, %lu waiting\nLast upload: %s\nLatest: %s",
                 free_text,
                 total_text,
                 static_cast<unsigned>(DiskStats::freeDiskSpacePercent()),
                 static_cast<unsigned long>(DiskStats::totalRecFilesStored()),
                 static_cast<unsigned long>(DiskStats::totalRecFilesNotUploaded()),
                 last_upload,
                 latest_file);
    }
    else
    {
        snprintf(text,
                 sizeof(text),
                 "System Info\nDisk: unavailable\nREC scan: unavailable\nLast upload: %s\nLatest: %s",
                 last_upload,
                 latest_file);
    }

    show_info_dialog(text, FLYGUI_VIEW_MAIN);
}

void onclick_main_wifi()
{
    ESP_LOGI(MAINTAG, "main screen wifi selected");
    ScrollView* scroll_view = get_scroll_view();
    if (scroll_view && gui)
    {
        scroll_view->populateWifi(wifi_manager);
        gui->showView(FLYGUI_VIEW_SCROLL);
    }
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

void conn_waiting_cancel()
{
    ESP_LOGI(MAINTAG, "connection waiting cancel selected");
}

void onclick_scroll_exit()
{
    ESP_LOGI(MAINTAG, "scroll view exit selected");
    if (gui)
    {
        gui->showView(FLYGUI_VIEW_MAIN);
    }
}

void onclick_bluetooth_host(int32_t value)
{
    ESP_LOGI(MAINTAG, "scroll bluetooth host selected: index=%ld", static_cast<long>(value));
}

void onclick_bluetooth_pair(int32_t value)
{
    ESP_LOGI(MAINTAG, "scroll bluetooth pair selected: task=%ld", static_cast<long>(value));
}

void onclick_wifi_scan_and_connect(int32_t value)
{
    ESP_LOGI(MAINTAG, "scroll wifi scan/connect selected: task=%ld", static_cast<long>(value));
    if (!wifi_manager)
    {
        ESP_LOGW(MAINTAG, "cannot scan/connect: Wi-Fi manager is not available");
        return;
    }

    if (!wifi_manager->scanAndConnect())
    {
        ESP_LOGW(MAINTAG, "could not start Wi-Fi scan/connect: %s", wifi_manager->statusName());
    }
}

void onclick_wifi_station(int32_t value)
{
    ESP_LOGI(MAINTAG, "scroll wifi station selected: index=%ld", static_cast<long>(value));
}

void onclick_wifi_ap(int32_t value)
{
    ESP_LOGI(MAINTAG, "scroll wifi ap selected: index=%ld", static_cast<long>(value));
}

void onclick_cloud_upload(int32_t value)
{
    ESP_LOGI(MAINTAG, "scroll cloud upload selected: index=%ld", static_cast<long>(value));
}

void onclick_ntp_sync(int32_t value)
{
    ESP_LOGI(MAINTAG, "scroll ntp sync selected: task=%ld", static_cast<long>(value));
}

void onclick_bt_show_info(int32_t value)
{
    ESP_LOGI(MAINTAG, "scroll bluetooth info selected: task=%ld", static_cast<long>(value));

    esp_bd_addr_t bdaddr = {};
    char          bdaddr_text[18] = "unknown";
    if (BtManager::localBdaddr(bdaddr))
    {
        format_bdaddr(bdaddr, bdaddr_text, sizeof(bdaddr_text));
    }

    char text[160];
    snprintf(text,
             sizeof(text),
             "Bluetooth\nName: %s\nBDADDR: %s",
             BtManager::localDeviceName(),
             bdaddr_text);
    show_info_dialog(text, FLYGUI_VIEW_SCROLL);
}

void onclick_wifi_show_info(int32_t value)
{
    ESP_LOGI(MAINTAG, "scroll wifi info selected: task=%ld", static_cast<long>(value));

    const wifi_item_t* item = wifi_manager ? (wifi_manager->connectedWifi() ? wifi_manager->connectedWifi() : wifi_manager->activeWifi()) : nullptr;
    const bool         is_ap = wifi_manager && wifi_manager->status() == WifiManager::Status::AccessPoint;
    const char*        ssid = item && item->ssid ? item->ssid : "";
    const IPAddress    ip   = is_ap ? WiFi.softAPIP() : WiFi.localIP();
    const String       ip_text = ip.toString();

    char text[192];
    if (is_ap && item && item->password && item->password[0] != '\0')
    {
        snprintf(text,
                 sizeof(text),
                 "Wi-Fi\nSSID: %s\nIP: %s\nPassword: %s",
                 ssid,
                 ip_text.c_str(),
                 item->password);
    }
    else
    {
        snprintf(text,
                 sizeof(text),
                 "Wi-Fi\nSSID: %s\nIP: %s",
                 ssid[0] != '\0' ? ssid : "(not connected)",
                 ip_text.c_str());
    }

    show_info_dialog(text, FLYGUI_VIEW_SCROLL);
}

static bool show_info_dialog(const char* text, uint16_t next_view)
{
    ModalDialog* dialog = get_modal_dialog();
    if (!dialog || !gui)
    {
        return false;
    }

    dialog->configure(sprit_info_100,
                      SPRIT_INFO_100_BYTES,
                      SPRIT_INFO_100_WIDTH,
                      SPRIT_INFO_100_HEIGHT,
                      text,
                      next_view);
    return gui->showView(FLYGUI_VIEW_MODAL_DIALOG);
}

static void on_wifi_scan_finished(const wifi_item_t* item)
{
    if (!wifi_manager)
    {
        return;
    }

    if (!item)
    {
        ESP_LOGW(MAINTAG, "Wi-Fi scan/connect did not find a configured station");
        return;
    }

    ESP_LOGI(MAINTAG, "Wi-Fi scan/connect found \"%s\", starting connection", item->ssid ? item->ssid : "");
    if (!wifi_manager->connectToHotspot(item))
    {
        ESP_LOGW(MAINTAG, "could not connect to scanned Wi-Fi station: %s", wifi_manager->statusName());
    }
}
