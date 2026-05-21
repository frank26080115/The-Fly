#include "thefly_common.h"
#include <Arduino.h>
#include "nvs_flash.h"
#include "M5Unified.h"
#include "FlyGui.h"
#include "ApModeView.h"
#include "BluetoothDeviceView.h"
#include "ConnWaitingView.h"
#include "ErrorView.h"
#include "FileListView.h"
#include "MainScreenView.h"
#include "ModalDialog.h"
#include "RecordingView/RecordingView.h"
#include "ScrollView/ScrollView.h"
#include "SplashView.h"
#include "UploadProgressView.h"
#include "WebActionView.h"
#include "WifiChooserView.h"
#include <stdarg.h>
#include <stdio.h>

constexpr const char* TAG = "all_init.cpp";

RTC_DATA_ATTR uint32_t reset_flag  = 0;
RTC_DATA_ATTR uint32_t reset_magic = 0;
bool reset_was_magic = false;

extern FlyGui* gui;
extern void conn_waiting_cancel(uint32_t pressDurationMs);
extern void onclick_scroll_exit(uint32_t pressDurationMs);
extern void onclick_bluetooth_host(int32_t value, uint32_t pressDurationMs);
extern void onclick_bluetooth_pair(int32_t value, uint32_t pressDurationMs);
extern void onclick_wifi_scan_and_connect(int32_t value, uint32_t pressDurationMs);
extern void onclick_wifi_station(int32_t value, uint32_t pressDurationMs);
extern void onclick_wifi_ap(int32_t value, uint32_t pressDurationMs);
extern void onclick_cloud_upload(int32_t value, uint32_t pressDurationMs);
extern void onclick_ntp_sync(int32_t value, uint32_t pressDurationMs);
extern void onclick_bt_show_info(int32_t value, uint32_t pressDurationMs);
extern void onclick_wifi_show_info(int32_t value, uint32_t pressDurationMs);

namespace
{
SplashView          g_splash_view;
MainScreenView      g_main_screen_view;
BluetoothDeviceView g_bluetooth_device_view;
RecordingView       g_recording_view;
WifiChooserView     g_wifi_chooser_view;
WebActionView       g_web_action_view;
ApModeView          g_ap_mode_view;
UploadProgressView  g_upload_progress_view;
FileListView        g_file_list_view;
ErrorView           g_error_view;
ModalDialog         g_modal_dialog;
ConnWaitingView     g_conn_waiting_view(CONN_WAITING_BLUETOOTH_CONNECTING, "", conn_waiting_cancel);
ScrollView          g_scroll_view(FLYGUI_VIEW_SCROLL, onclick_scroll_exit);
uint16_t            g_conn_waiting_return_view_id = FLYGUI_VIEW_MAIN;
} // namespace

bool init_nvs();
void check_reset_flag();
void init_m5();
void init_gui();

void all_init()
{
    Serial.begin(115200);
    const bool nvs_ok = init_nvs();
    check_reset_flag();
    init_m5();
    init_gui();

    if (!nvs_ok)
    {
        show_fatal_error_f(true, "NVS init failed");
    }
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
    gui = new FlyGui();
    thefly_display.setBrightness(255);
    thefly_display.setColorDepth(16);
    thefly_display.fillScreen(TFT_BLACK);

    gui->addView(g_splash_view);
    gui->addView(g_main_screen_view);
    gui->addView(g_bluetooth_device_view);
    gui->addView(g_recording_view);
    gui->addView(g_wifi_chooser_view);
    gui->addView(g_web_action_view);
    gui->addView(g_ap_mode_view);
    gui->addView(g_upload_progress_view);
    gui->addView(g_file_list_view);
    gui->addView(g_error_view);
    gui->addView(g_modal_dialog);
    gui->addView(g_conn_waiting_view);
    gui->addView(g_scroll_view);

    g_scroll_view.setOnClickBluetoothHost(onclick_bluetooth_host);
    g_scroll_view.setOnClickBluetoothPair(onclick_bluetooth_pair);
    g_scroll_view.setOnClickWifiScanAndConnect(onclick_wifi_scan_and_connect);
    g_scroll_view.setOnClickWifiStation(onclick_wifi_station);
    g_scroll_view.setOnClickWifiAp(onclick_wifi_ap);
    g_scroll_view.setOnClickCloudUpload(onclick_cloud_upload);
    g_scroll_view.setOnClickNtpSync(onclick_ntp_sync);
    g_scroll_view.setOnClickBtShowInfo(onclick_bt_show_info);
    g_scroll_view.setOnClickWifiShowInfo(onclick_wifi_show_info);
}

ScrollView* get_scroll_view()
{
    return &g_scroll_view;
}

ModalDialog* get_modal_dialog()
{
    return &g_modal_dialog;
}

uint16_t conn_waiting_return_view_id()
{
    return g_conn_waiting_return_view_id;
}

void remember_conn_waiting_return_view()
{
    if (!gui || !gui->currentView() || gui->currentView()->id() == FLYGUI_VIEW_CONN_WAITING)
    {
        g_conn_waiting_return_view_id = FLYGUI_VIEW_MAIN;
        return;
    }

    g_conn_waiting_return_view_id = gui->currentView()->id();
}

bool show_conn_waiting_bluetooth(const char* targetName)
{
    if (!gui)
    {
        return false;
    }

    remember_conn_waiting_return_view();
    g_conn_waiting_view.configure(CONN_WAITING_BLUETOOTH_CONNECTING, targetName);
    g_conn_waiting_view.setCancelCallback(conn_waiting_cancel);
    return gui->showView(FLYGUI_VIEW_CONN_WAITING);
}

bool show_conn_waiting_bluetooth_pairing()
{
    if (!gui)
    {
        return false;
    }

    remember_conn_waiting_return_view();
    g_conn_waiting_view.configure(CONN_WAITING_BLUETOOTH_PAIRING, "");
    g_conn_waiting_view.setCancelCallback(conn_waiting_cancel);
    return gui->showView(FLYGUI_VIEW_CONN_WAITING);
}

bool show_recording_view_bluetooth()
{
    if (!gui)
    {
        return false;
    }

    g_recording_view.configureBluetoothMode();
    return gui->showView(FLYGUI_VIEW_RECORDING);
}

bool show_recording_view_memo()
{
    if (!gui)
    {
        return false;
    }

    const bool started = g_recording_view.beginMemoRecording();
    return started && gui->showView(FLYGUI_VIEW_RECORDING);
}

void show_splash()
{
    if (!gui || !gui->showView(FLYGUI_VIEW_SPLASH))
    {
        show_fatal_error_f(true, "Failed to show splash view");
    }
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

void show_fatal_error_f(bool fatal, const char* format, ...)
{
    char message[256];

    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format ? format : "", args);
    va_end(args);

    ESP_LOGE(TAG, "%s", message);

    if (!gui)
    {
        while (fatal)
        {
            delay(1000);
        }
        return;
    }

    FlyGuiView* previous_view    = gui->currentView();
    const uint16_t previous_view_id = previous_view ? previous_view->id() : FLYGUI_VIEW_MAIN;

    g_error_view.setMessage(message, fatal);
    if (!gui->showView(FLYGUI_VIEW_ERROR))
    {
        ESP_LOGE(TAG, "Failed to show error view");
        while (fatal)
        {
            delay(1000);
        }
        return;
    }

    gui->redraw(true);
    while (fatal || !g_error_view.dismissed())
    {
        gui->poll();
        delay(10);
    }

    if (!fatal && previous_view_id != FLYGUI_VIEW_ERROR)
    {
        gui->showView(previous_view_id);
    }
}
