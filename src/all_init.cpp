#include "thefly_common.h"
#include <Arduino.h>
#include "nvs_flash.h"
#include "M5Unified.h"
#include "FlyGui.h"
#include "ConnWaitingView.h"
#include "ErrorView.h"
#include "FirmwareUpdateView.h"
#include "MainScreenView.h"
#include "ModalDialog.h"
#include "PlaybackView.h"
#include "PinPadView.h"
#include "QrCodeView.h"
#include "RecordingView/RecordingView.h"
#include "ScrollView/ScrollView.h"
#include "SplashView.h"
#include "WifiApModeView.h"
#include "WifiStaModeView.h"
#include "main_callbacks.h"
#include "BattTracker.h"

#ifdef BUILD_CLOUD_FEATURES
#include "CloudUploadView.h"
#endif

constexpr const char* TAG = "all_init.cpp";

RTC_DATA_ATTR uint32_t reset_flag      = 0;
RTC_DATA_ATTR uint32_t reset_magic     = 0;
bool                   reset_was_magic = false;
bool                   g_nvs_ready     = false;

extern FlyGui* gui;

namespace
{
SplashView         g_splash_view;
MainScreenView     g_main_screen_view;
RecordingView      g_recording_view;
ErrorView          g_error_view;
ModalDialog        g_modal_dialog;
ConnWaitingView    g_conn_waiting_view(CONN_WAITING_BLUETOOTH_CONNECTING, "", conn_waiting_cancel);
ScrollView         g_scroll_view(FLYGUI_VIEW_SCROLL, onclick_scroll_exit);
WifiApModeView     g_wifi_ap_mode_view;
WifiStaModeView    g_wifi_sta_mode_view;
FirmwareUpdateView g_firmware_update_view;
PlaybackView       g_playback_view;
PinPadView         g_pin_pad_view;
QrCodeView         g_qr_code_view;
#ifdef BUILD_CLOUD_FEATURES
CloudUploadView g_cloud_upload_view(cloud_upload_cancel);
#endif
} // namespace

bool        init_nvs();
void        check_reset_flag();
void        init_m5();
void        init_gui();

void all_init()
{
    Serial.begin(115200, SERIAL_8N1, -1, 1); // RXD0/GPIO3 unused; TXD0/GPIO1 may be reclaimed for SGTL5000 MCLK.
    g_nvs_ready = init_nvs();
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
    gui = new FlyGui();
    thefly_display.setBrightness(255);
    thefly_display.setColorDepth(16);
    thefly_display.fillScreen(TFT_BLACK);

    gui->addView(g_splash_view);
    gui->addView(g_main_screen_view);
    gui->addView(g_recording_view);
    gui->addView(g_error_view);
    gui->addView(g_modal_dialog);
    gui->addView(g_conn_waiting_view);
    gui->addView(g_scroll_view);
    gui->addView(g_wifi_ap_mode_view);
    gui->addView(g_wifi_sta_mode_view);
    gui->addView(g_firmware_update_view);
    gui->addView(g_playback_view);
    gui->addView(g_pin_pad_view);
    gui->addView(g_qr_code_view);

    g_scroll_view.setOnClickBluetoothHost(onclick_bluetooth_host);
    g_scroll_view.setOnClickBluetoothPair(onclick_bluetooth_pair);
    g_scroll_view.setOnClickWifiScanAndConnect(onclick_wifi_scan_and_connect);
    g_scroll_view.setOnClickWifiStation(onclick_wifi_station);
    g_scroll_view.setOnClickWifiAp(onclick_wifi_ap);
    g_scroll_view.setOnClickNtpSync(onclick_ntp_sync);
    g_scroll_view.setOnClickBtShowInfo(onclick_bt_show_info);
    g_scroll_view.setOnClickWifiShowInfo(onclick_wifi_show_info);
    g_scroll_view.setOnClickFilePlayable(onclick_file_playable);
    g_scroll_view.setOnClickFileListShowInfo(onclick_filelist_show_info);
#ifdef BUILD_CLOUD_FEATURES
    gui->addView(g_cloud_upload_view);
    g_scroll_view.setOnClickCloudUpload(onclick_cloud_upload);
#endif
}

ScrollView* get_view_scroll()
{
    return &g_scroll_view;
}

ModalDialog* get_view_modal_dialog()
{
    return &g_modal_dialog;
}

MainScreenView* get_view_main_screen()
{
    return &g_main_screen_view;
}

ErrorView* get_view_error()
{
    return &g_error_view;
}

RecordingView* get_view_recording()
{
    return &g_recording_view;
}

ConnWaitingView* get_view_conn_waiting()
{
    return &g_conn_waiting_view;
}

#ifdef BUILD_CLOUD_FEATURES
CloudUploadView* get_view_cloud_upload()
{
    return &g_cloud_upload_view;
}
#endif

WifiStaModeView* get_view_wifi_sta_mode()
{
    return &g_wifi_sta_mode_view;
}

FirmwareUpdateView* get_view_firmware_update()
{
    return &g_firmware_update_view;
}

PlaybackView* get_view_playback()
{
    return &g_playback_view;
}

PinPadView* get_view_pin_pad()
{
    return &g_pin_pad_view;
}

QrCodeView* get_view_qr_code()
{
    return &g_qr_code_view;
}

void show_splash()
{
    if (!gui || !gui->showView(FLYGUI_VIEW_SPLASH))
    {
        show_boot_error_f(true, "Failed to show splash view");
    }
}

void check_reset_flag()
{
    if (reset_magic == RESET_MAGIC)
    {
        DBG_LOGI(TAG, "Booted after flagged reset: flag=%u\n", reset_flag);

        reset_was_magic = true;

        // Clear it so it is one-shot
        reset_magic = 0;
        reset_flag  = 0;
    }
    else
    {
        DBG_LOGI(TAG, "Normal boot / power-on / unflagged reset\n");
    }
}
