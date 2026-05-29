#include "thefly_common.h"
#include "main_callbacks.h"
#include <Arduino.h>
#include "M5Unified.h"
#include <WiFi.h>
#include "nvs_flash.h"
#include "AudioManager.h"
#include "AudioFileRecorder.h"
#include "BattTracker.h"
#include "BluetoothManager.h"
#include "BtHostList.h"
#include "Aegis.h"
#include "ConnWaitingView.h"
#include "DiskStats.h"
#include "Hotel.h"
#include "FlyGui.h"
#include "Display.h"
#include "MainScreenView.h"
#include "MicroSdCard.h"
#include "ModalDialog.h"
#include "NtpSync.h"
#include "RecordingView/RecordingView.h"
#include "RecordingView/RecordingViewCallbacks.h"
#include "ScrollView/ScrollView.h"
#include "ShutdownView.h"
#include "WebServer.h"
#include "WifiManager.h"
#include "WifiStaModeView.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sprites.h"
#include "utilfuncs.h"
#include "all_tests.h"
#include <string.h>

#ifdef BUILD_CLOUD_FEATURES
#include "CloudUpload.h"
#include "CloudUploadView.h"
#endif

constexpr const char* MAINTAG = "main.cpp";

extern void all_init();
extern void show_splash();
extern void draw_splash_boot_info();
extern ScrollView* all_init_scroll_view();
extern ModalDialog* all_init_modal_dialog();
extern MainScreenView* all_init_main_screen_view();
extern RecordingView* all_init_recording_view();
extern ConnWaitingView* all_init_conn_waiting_view();
extern WifiStaModeView* all_init_wifi_sta_mode_view();

#ifdef BUILD_CLOUD_FEATURES
extern CloudUploadView* all_init_cloud_upload_view();
#endif

TaskHandle_t loopTask_core0_Handle = NULL;
static void  loopTask_core0(void* pvParameters);
bool         show_info_dialog(const char* text, uint16_t next_view);
bool         show_error_dialog(const char* text, uint16_t next_view);
static bool  show_pairing_success_dialog(const BtManager::PairedDevice& device);
bool         bluetooth_recording_state(BtManager::State state);
static void  handle_pending_bluetooth_recording();
static void  handle_pending_bluetooth_pairing();
static void  handle_pending_bluetooth_connect_failed();
#ifdef BUILD_CLOUD_FEATURES
static void  handle_pending_cloud_upload_complete();
#endif
static void  handle_pending_ntp_sync_complete();
static void  handle_wifi_connection_waiting();
static void  handle_wifi_station_connected();
void         show_wifi_connection_failed(const char* text);
bool         connect_to_bluetooth_host(const bt_host_item_t* host, const char* source);
void         request_bluetooth_disconnect();
static const char* info_dialog_text_with_tamper_code(const char* text, char* buffer, size_t buffer_size);
static const char* ntp_error_name(NtpSync::Error error);

FlyGui* gui;
M5GFX& thefly_display = M5.Display;
BtHostList* bt_host_list;
WifiManager* wifi_manager;
volatile bool g_pending_bluetooth_recording = false;
volatile bool g_pending_bluetooth_disconnect = false;
volatile bool g_pending_bluetooth_pairing = false;
volatile bool g_bluetooth_connect_waiting = false;
volatile bool g_pending_bluetooth_connect_failed = false;
volatile bool g_suppress_bluetooth_auto_recording = false;
bool g_wifi_connect_waiting = false;
NtpSync g_ntp_sync;
bool g_ntp_sync_waiting = false;
bool g_ntp_sync_completion_suppressed = false;
volatile bool g_pending_ntp_sync_complete = false;
NtpSync::Result g_pending_ntp_sync_result = {};
static uint16_t g_conn_waiting_return_view_id = FLYGUI_VIEW_MAIN;
BtManager::PairedDevice g_pending_paired_device = {};

#ifdef BUILD_CLOUD_FEATURES
CloudUpload g_cloud_upload;
volatile bool g_pending_cloud_upload_complete = false;
CloudUpload::Status g_pending_cloud_upload_status = {};
#endif

ScrollView* get_scroll_view()
{
    return all_init_scroll_view();
}

ModalDialog* get_modal_dialog()
{
    return all_init_modal_dialog();
}

void show_main_memo_starting_feedback()
{
    if (MainScreenView* view = all_init_main_screen_view())
    {
        view->showMemoStartingFeedback();
    }
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

static bool show_conn_waiting_mode(ConnWaitingMode mode, const char* targetName)
{
    ConnWaitingView* view = all_init_conn_waiting_view();
    if (!gui || !view)
    {
        return false;
    }

    remember_conn_waiting_return_view();
    view->configure(mode, targetName);
    view->setCancelCallback(conn_waiting_cancel);
    return gui->showView(FLYGUI_VIEW_CONN_WAITING);
}

bool show_conn_waiting_bluetooth(const char* targetName)
{
    return show_conn_waiting_mode(CONN_WAITING_BLUETOOTH_CONNECTING, targetName);
}

bool show_conn_waiting_bluetooth_pairing()
{
    return show_conn_waiting_mode(CONN_WAITING_BLUETOOTH_PAIRING, "");
}

bool show_conn_waiting_wifi_connecting(const char* targetName)
{
    return show_conn_waiting_mode(CONN_WAITING_WIFI_CONNECTING, targetName);
}

bool show_conn_waiting_wifi_scanning(const char* targetName)
{
    return show_conn_waiting_mode(CONN_WAITING_WIFI_SCANNING, targetName);
}

bool show_conn_waiting_ntp_sync(const char* targetName)
{
    return show_conn_waiting_mode(CONN_WAITING_NTP_SYNC, targetName);
}

void update_conn_waiting_wifi_target(const char* targetName)
{
    if (ConnWaitingView* view = all_init_conn_waiting_view())
    {
        view->configure(CONN_WAITING_WIFI_CONNECTING, targetName);
        view->setCancelCallback(conn_waiting_cancel);
    }
}

#ifdef BUILD_CLOUD_FEATURES
bool show_cloud_upload_view(CloudUpload* uploader, const char* targetName)
{
    CloudUploadView* view = all_init_cloud_upload_view();
    if (!gui || !view)
    {
        return false;
    }

    view->configureUpload(uploader, targetName);
    view->setCancelCallback(cloud_upload_cancel);
    return gui->showView(FLYGUI_VIEW_UPLOAD_PROGRESS);
}
#endif

bool show_recording_view_bluetooth()
{
    RecordingView* view = all_init_recording_view();
    if (!gui || !view)
    {
        return false;
    }

    view->configureBluetoothMode();
    return gui->showView(FLYGUI_VIEW_RECORDING);
}

bool promote_recording_view_memo_to_bluetooth()
{
    RecordingView* view = all_init_recording_view();
    if (!gui || !view || !view->promoteMemoToBluetoothMode())
    {
        return false;
    }

    if (gui->currentView() && gui->currentView()->id() == FLYGUI_VIEW_RECORDING)
    {
        gui->redraw(true);
        return true;
    }

    return gui->showView(FLYGUI_VIEW_RECORDING);
}

bool show_recording_view_memo()
{
    RecordingView* view = all_init_recording_view();
    if (!gui || !view)
    {
        return false;
    }

    const bool started = view->beginMemoRecording();
    return started && gui->showView(FLYGUI_VIEW_RECORDING);
}

bool show_wifi_ap_mode_view()
{
    return gui && gui->showView(FLYGUI_VIEW_AP_MODE);
}

bool show_wifi_sta_mode_view(bool showDismissButton)
{
    WifiStaModeView* view = all_init_wifi_sta_mode_view();
    if (!gui || !view)
    {
        return false;
    }

    view->configure(showDismissButton);
    return gui->showView(FLYGUI_VIEW_STA_MODE);
}

void setup()
{
    all_init();

    #ifdef RUN_BRINGUP_TEST
    run_test();
    #endif

    BattTracker::init();
    BattTracker::shutdownIfNeeded();

    if (reset_was_magic == false) {
        show_splash();
        draw_splash_boot_info();
    }

    if (!MicroSdCard::begin())
    {
        show_fatal_error_f(true, "microSD init failed");
    }

    wifi_manager = new WifiManager();
    #if BUILD_WITH_SECURITY_LEVEL <= 0
    if (wifi_manager && !wifi_manager->loadFromMicroSd())
    #else
    if (wifi_manager && !wifi_manager->loadFromNvs())
    #endif
    {
        show_fatal_error_f(false, "Wi-Fi configuration load failed: %s", wifi_manager->lastLoadResultName());
    }
    if (wifi_manager)
    {
        wifi_manager->setOnScanFinished(on_wifi_scan_finished);
    }
    if (gui && gui->currentView() && gui->currentView()->id() == FLYGUI_VIEW_SPLASH)
    {
        draw_splash_boot_info();
    }

    if (!AudioManager::init())
    {
        show_fatal_error_f(true, "AudioManager init failed");
    }

    BtManager::setStateChangedCallback(on_bluetooth_state_changed);
    BtManager::setPairedCallback(on_bluetooth_paired);
    BtManager::setAudioCallbacks(AudioManager::hfp_incoming_audio, AudioManager::hfp_outgoing_audio);
    BtManager::generateLegacyPinFromMac();
    ESP_LOGI(MAINTAG, "Bluetooth legacy pairing PIN: %s", BtManager::generatedLegacyPin());
    if (!BtManager::init(nullptr, AudioManager::hfp_incoming_audio, AudioManager::hfp_outgoing_audio, BtManager::generatedLegacyPin()))
    {
        show_fatal_error_f(true, "BluetoothManager init failed");
    }

    bt_host_list = new BtHostList();
    #if BUILD_WITH_SECURITY_LEVEL <= 0
    if (!bt_host_list->loadFromMicroSd())
    #else
    if (!bt_host_list->loadFromNvs())
    #endif
    {
        show_fatal_error_f(false, "Bluetooth host list load failed");
    }
    if (!bt_host_list->pruneBonds())
    {
        show_fatal_error_f(false, "Bluetooth bond pruning failed: %s", bt_host_list->lastLoadResultName());
    }

    if (!gui)
    {
        show_fatal_error_f(true, "GUI init failed");
    }
    else if (!gui->currentView())
    {
        if (!gui->showView(FLYGUI_VIEW_MAIN))
        {
            show_fatal_error_f(true, "Failed to show main view");
        }
    }

    xTaskCreateUniversal(loopTask_core0, "loopTask_core0", getArduinoLoopTaskStackSize(), NULL, 1, &loopTask_core0_Handle, 0);
}

void loop()
{
    // this is running on core 1
    if (ShutdownView::shutdownInProgress())
    {
        delay(1000);
        return;
    }

    BattTracker::poll();

    handle_pending_bluetooth_pairing();
    handle_pending_bluetooth_connect_failed();
    handle_pending_bluetooth_recording();
    #ifdef BUILD_CLOUD_FEATURES
    handle_pending_cloud_upload_complete();
    #endif
    handle_pending_ntp_sync_complete();
    gui->poll();
    if (AudioFileRecorder::needsPump())
    {
        AudioFileRecorder::pump();
    }
    if (wifi_manager) {
        wifi_manager->poll();
        handle_wifi_connection_waiting();
    }
    Hotel::pollCore1();
    taskYIELD();
}

static void loopTask_core0(void* pvParameters)
{
    // this is running on core 0
    while (true)
    {
        if (ShutdownView::shutdownInProgress())
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (g_pending_bluetooth_disconnect)
        {
            g_pending_bluetooth_disconnect = false;
            BtManager::disconnect();
        }

        BtManager::poll();
        AudioManager::pump_task();
        Hotel::pollCore0();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

bool bluetooth_recording_state(BtManager::State state)
{
    return state == BtManager::State::Connected || state == BtManager::State::AudioAvailable;
}

static void handle_pending_bluetooth_pairing()
{
    if (!g_pending_bluetooth_pairing)
    {
        return;
    }
    g_pending_bluetooth_pairing = false;

    if (bt_host_list)
    {
        if (!bt_host_list->insert(g_pending_paired_device.name, g_pending_paired_device.mac))
        {
            ESP_LOGW(MAINTAG, "failed to add paired Bluetooth host: %s", bt_host_list->lastLoadResultName());
            show_fatal_error_f(false, "Bluetooth pair saved in NVS, but host list insert failed: %s", bt_host_list->lastLoadResultName());
        }
        else if (!bt_host_list->saveToNvs())
        {
            ESP_LOGW(MAINTAG, "failed to save Bluetooth host list: %s", bt_host_list->lastLoadResultName());
            show_fatal_error_f(false, "Bluetooth pair saved in NVS, but Bluetooth host list save failed: %s", bt_host_list->lastLoadResultName());
        }
    }

    ScrollView* scroll_view = get_scroll_view();
    if (scroll_view)
    {
        scroll_view->populateBluetooth(bt_host_list);
    }

    g_suppress_bluetooth_auto_recording = true;
    const bool pairing_dialog_shown = show_pairing_success_dialog(g_pending_paired_device);
    if (!pairing_dialog_shown)
    {
        g_suppress_bluetooth_auto_recording = false;
        if (gui && gui->currentView() && gui->currentView()->id() == FLYGUI_VIEW_CONN_WAITING)
        {
            gui->showView(FLYGUI_VIEW_SCROLL);
        }
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
        if (promote_recording_view_memo_to_bluetooth())
        {
            ESP_LOGI(MAINTAG, "Bluetooth connected while memo recording; promoting active recording view");
            if (!RecordingViewCallbacks::promoteMemoRecordingToBluetooth())
            {
                ESP_LOGW(MAINTAG, "failed to promote memo audio routing for Bluetooth");
            }
        }
        return;
    }

    ESP_LOGI(MAINTAG, "Bluetooth recording trigger accepted at state: %s", BtManager::stateName(BtManager::state()));
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

static void handle_pending_bluetooth_connect_failed()
{
    if (!g_pending_bluetooth_connect_failed)
    {
        return;
    }
    g_pending_bluetooth_connect_failed = false;

    ESP_LOGW(MAINTAG, "Bluetooth host connection failed before HFP service level connection");

    ScrollView* scroll_view = get_scroll_view();
    if (scroll_view)
    {
        scroll_view->populateBluetooth(bt_host_list);
    }

    if (!show_error_dialog("Unable to connect to host\nMaybe try connecting from the host.", FLYGUI_VIEW_SCROLL) && gui)
    {
        gui->showView(FLYGUI_VIEW_SCROLL);
    }
}

#ifdef BUILD_CLOUD_FEATURES
static void handle_pending_cloud_upload_complete()
{
    if (!g_pending_cloud_upload_complete)
    {
        return;
    }
    g_pending_cloud_upload_complete = false;

    const CloudUpload::Status status = g_pending_cloud_upload_status;
    const bool                succeeded = status.state == CloudUpload::State::Done && status.error == CloudUpload::Error::None;

    ModalDialog* dialog = get_modal_dialog();
    if (!dialog || !gui)
    {
        ESP_LOGW(MAINTAG, "cloud upload complete but modal dialog is unavailable; rebooting");
        delay(50);
        esp_restart();
        return;
    }

    char text[240] = {};
    if (succeeded)
    {
        snprintf(text,
                 sizeof(text),
                 "Cloud upload complete\n%lu/%lu files uploaded.",
                 static_cast<unsigned long>(status.files_succeeded),
                 static_cast<unsigned long>(status.files_total));
        dialog->configure(sprite_thumbsup_100,
                          SPRITE_THUMBSUP_100_BYTES,
                          SPRITE_THUMBSUP_100_WIDTH,
                          SPRITE_THUMBSUP_100_HEIGHT,
                          text,
                          FLYGUI_VIEW_MODAL_DIALOG,
                          on_cloud_upload_dialog_dismissed);
    }
    else
    {
        snprintf(text,
                 sizeof(text),
                 "Cloud upload failed\n%s\n%lu/%lu uploaded, %lu errs",
                 status.message[0] != '\0' ? status.message : "unknown error",
                 static_cast<unsigned long>(status.files_succeeded),
                 static_cast<unsigned long>(status.files_total),
                 static_cast<unsigned long>(status.files_failed));
        dialog->configure(sprite_warning_100,
                          SPRITE_WARNING_100_BYTES,
                          SPRITE_WARNING_100_WIDTH,
                          SPRITE_WARNING_100_HEIGHT,
                          text,
                          FLYGUI_VIEW_MODAL_DIALOG,
                          on_cloud_upload_dialog_dismissed);
    }

    gui->showView(FLYGUI_VIEW_MODAL_DIALOG);
}
#endif

static void handle_pending_ntp_sync_complete()
{
    if (!g_pending_ntp_sync_complete)
    {
        return;
    }
    g_pending_ntp_sync_complete = false;

    const NtpSync::Result result = g_pending_ntp_sync_result;
    const bool            suppress_dialog = g_ntp_sync_completion_suppressed;
    g_ntp_sync_waiting = false;
    g_ntp_sync_completion_suppressed = false;

    if (suppress_dialog)
    {
        return;
    }

    ModalDialog* dialog = get_modal_dialog();
    if (!dialog || !gui)
    {
        ESP_LOGW(MAINTAG, "NTP sync complete but modal dialog is unavailable");
        return;
    }

    char text[160] = {};
    if (result.status == NtpSync::Status::Done && result.error == NtpSync::Error::None)
    {
        snprintf(text,
                 sizeof(text),
                 "Time synchronized\n%s\nOffset: %+llds",
                 result.rtc_write_succeeded ? "RTC updated" : "RTC not updated",
                 static_cast<long long>(result.rtc_offset_seconds));
        dialog->configure(sprite_thumbsup_100,
                          SPRITE_THUMBSUP_100_BYTES,
                          SPRITE_THUMBSUP_100_WIDTH,
                          SPRITE_THUMBSUP_100_HEIGHT,
                          text,
                          FLYGUI_VIEW_SCROLL);
    }
    else if (result.status == NtpSync::Status::Cancelled || result.error == NtpSync::Error::Cancelled)
    {
        dialog->configure(sprite_info_100,
                          SPRITE_INFO_100_BYTES,
                          SPRITE_INFO_100_WIDTH,
                          SPRITE_INFO_100_HEIGHT,
                          "NTP sync cancelled",
                          FLYGUI_VIEW_SCROLL);
    }
    else
    {
        snprintf(text, sizeof(text), "NTP sync failed\n%s", ntp_error_name(result.error));
        dialog->configure(sprite_warning_100,
                          SPRITE_WARNING_100_BYTES,
                          SPRITE_WARNING_100_WIDTH,
                          SPRITE_WARNING_100_HEIGHT,
                          text,
                          FLYGUI_VIEW_SCROLL);
    }

    gui->showView(FLYGUI_VIEW_MODAL_DIALOG);
}

static void handle_wifi_connection_waiting()
{
    if (!g_wifi_connect_waiting || !wifi_manager)
    {
        return;
    }

    switch (wifi_manager->status())
    {
    case WifiManager::Status::StationConnected:
        g_wifi_connect_waiting = false;
        handle_wifi_station_connected();
        break;
    case WifiManager::Status::NoKnownNetwork:
        show_wifi_connection_failed("No configured Wi-Fi was found");
        break;
    case WifiManager::Status::ScanFailed:
        show_wifi_connection_failed("Wi-Fi scan failed");
        break;
    case WifiManager::Status::ConnectFailed:
        show_wifi_connection_failed("Wi-Fi connection failed");
        break;
    default:
        break;
    }
}

static void handle_wifi_station_connected()
{
    if (!wifi_manager)
    {
        return;
    }

    ESP_LOGI(MAINTAG,
             "Wi-Fi station connected: ssid=%s ip=%s gateway=%s",
             wifi_manager->connectedWifi() ? wifi_manager->connectedWifi()->ssid : "",
             WiFi.localIP().toString().c_str(),
             WiFi.gatewayIP().toString().c_str());

    if (!WebServer::init())
    {
        show_fatal_error_f(false, "Wi-Fi web server failed to start");
        return;
    }

    ScrollView* scroll_view = get_scroll_view();
    if (!scroll_view || !gui)
    {
        show_fatal_error_f(false, "Wi-Fi action view is unavailable");
        return;
    }

    if (!scroll_view->populateCloud(wifi_manager))
    {
        show_fatal_error_f(false, "Wi-Fi action list failed");
        return;
    }

    if (!gui->showView(FLYGUI_VIEW_SCROLL))
    {
        show_fatal_error_f(false, "Wi-Fi action view failed");
    }
}

void show_wifi_connection_failed(const char* text)
{
    g_wifi_connect_waiting = false;

    ScrollView* scroll_view = get_scroll_view();
    if (scroll_view)
    {
        scroll_view->populateWifi(wifi_manager);
    }

    if (!show_error_dialog(text ? text : "Wi-Fi connection failed", FLYGUI_VIEW_SCROLL) && gui)
    {
        gui->showView(FLYGUI_VIEW_SCROLL);
    }
}

bool connect_to_bluetooth_host(const bt_host_item_t* host, const char* source)
{
    if (!host)
    {
        show_fatal_error_f(false, "No Bluetooth host configured");
        return false;
    }

    char bdaddr_text[18] = {};
    format_bdaddr(host->bdaddr, bdaddr_text, sizeof(bdaddr_text));
    ESP_LOGI(MAINTAG,
             "connecting to Bluetooth host from %s: name=%s bdaddr=%s",
             source ? source : "unknown",
             bt_host_display_name(host),
             bdaddr_text);

    if (bluetooth_recording_state(BtManager::state()) && bda_equal(BtManager::connectedMac(), host->bdaddr))
    {
        ESP_LOGI(MAINTAG, "Bluetooth host is already connected; starting recording without a new HFP connection");
        g_bluetooth_connect_waiting = false;
        g_pending_bluetooth_connect_failed = false;
        g_pending_bluetooth_recording = true;
        return true;
    }

    g_bluetooth_connect_waiting = true;
    g_pending_bluetooth_connect_failed = false;

    const BtManager::Result result = BtManager::connectToMac(host->bdaddr);
    if (result != BtManager::Result::Ok)
    {
        g_bluetooth_connect_waiting = false;
        ESP_LOGW(MAINTAG, "Bluetooth connection start failed: %s", BtManager::resultName(result));
        show_fatal_error_f(false, "Bluetooth connection failed: %s", BtManager::resultName(result));
        return false;
    }

    if (!show_conn_waiting_bluetooth(bt_host_display_name(host)))
    {
        g_bluetooth_connect_waiting = false;
        ESP_LOGE(MAINTAG, "failed to show Bluetooth connection waiting view");
        BtManager::disconnect();
        show_fatal_error_f(false, "Bluetooth connection view failed");
        return false;
    }

    return true;
}

void request_bluetooth_disconnect()
{
    g_pending_bluetooth_disconnect = true;
}

bool show_info_dialog(const char* text, uint16_t next_view)
{
    ModalDialog* dialog = get_modal_dialog();
    if (!dialog || !gui)
    {
        return false;
    }

    char        dialog_text[256] = {};
    const char* shown_text = info_dialog_text_with_tamper_code(text, dialog_text, sizeof(dialog_text));

    dialog->configure(sprite_info_100,
                      SPRITE_INFO_100_BYTES,
                      SPRITE_INFO_100_WIDTH,
                      SPRITE_INFO_100_HEIGHT,
                      shown_text,
                      next_view);
    return gui->showView(FLYGUI_VIEW_MODAL_DIALOG);
}

static const char* info_dialog_text_with_tamper_code(const char* text, char* buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
    {
        return text ? text : "";
    }

    strlcpy(buffer, text ? text : "", buffer_size);
#if BUILD_WITH_SECURITY_LEVEL == 2
    uint32_t code = 0;
    if (Aegis::tamperEvidenceCode(code))
    {
        char line[24] = {};
        snprintf(line, sizeof(line), "\nTamper: %04lX", static_cast<unsigned long>((code >> 16) & 0xFFFF));
        strlcat(buffer, line, buffer_size);
    }
#endif
    return buffer;
}

static const char* ntp_error_name(NtpSync::Error error)
{
    switch (error)
    {
    case NtpSync::Error::None:
        return "None";
    case NtpSync::Error::AlreadyBusy:
        return "Already busy";
    case NtpSync::Error::InvalidArgument:
        return "Invalid configuration";
    case NtpSync::Error::WifiNotConnected:
        return "Wi-Fi is not connected";
    case NtpSync::Error::TaskCreateFailed:
        return "Task could not start";
    case NtpSync::Error::Cancelled:
        return "Cancelled";
    case NtpSync::Error::Timeout:
        return "Timed out";
    case NtpSync::Error::RtcReadFailed:
        return "RTC read failed";
    case NtpSync::Error::NtpReadFailed:
        return "NTP read failed";
    case NtpSync::Error::RtcWriteFailed:
        return "RTC write failed";
    case NtpSync::Error::NoNtpResult:
        return "No NTP result";
    default:
        return "Unknown error";
    }
}

bool show_error_dialog(const char* text, uint16_t next_view)
{
    ModalDialog* dialog = get_modal_dialog();
    if (!dialog || !gui)
    {
        return false;
    }

    dialog->configure(sprite_warning_100,
                      SPRITE_WARNING_100_BYTES,
                      SPRITE_WARNING_100_WIDTH,
                      SPRITE_WARNING_100_HEIGHT,
                      text,
                      next_view);
    return gui->showView(FLYGUI_VIEW_MODAL_DIALOG);
}

static bool show_pairing_success_dialog(const BtManager::PairedDevice& device)
{
    ModalDialog* dialog = get_modal_dialog();
    if (!dialog || !gui)
    {
        return false;
    }

    const char* name = device.name[0] != '\0' ? device.name : "Bluetooth device";
    char        text[160];
    snprintf(text, sizeof(text), "Pairing complete\n%s is now saved.", name);

    dialog->configure(sprite_thumbsup_100,
                      SPRITE_THUMBSUP_100_BYTES,
                      SPRITE_THUMBSUP_100_WIDTH,
                      SPRITE_THUMBSUP_100_HEIGHT,
                      text,
                      FLYGUI_VIEW_MODAL_DIALOG,
                      on_pairing_success_dialog_dismissed);
    return gui->showView(FLYGUI_VIEW_MODAL_DIALOG);
}
