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
#include "ExtCodec.h"
#include "FirmwareUpdateView.h"
#include "HapticsWrapper.h"
#include "MainScreenView.h"
#include "MicroSdCard.h"
#include "ModalDialog.h"
#include "PlaybackView.h"
#include "PinPadView.h"
#include "NtpSync.h"
#include "RecordingView/RecordingView.h"
#include "RecordingView/RecordingViewCallbacks.h"
#include "ScrollView/ScrollView.h"
#include "ShutdownView.h"
#include "WebServer.h"
#include "WifiManager.h"
#include "WifiStaModeView.h"
#include "dbg_log.h"
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

extern void                all_init();
extern void                show_splash();
extern void                draw_splash_boot_info();
extern void                draw_splash_idle_prompt();
extern ScrollView*         get_view_scroll();
extern ModalDialog*        get_view_modal_dialog();
extern RecordingView*      get_view_recording();
extern ConnWaitingView*    get_view_conn_waiting();
extern WifiStaModeView*    get_view_wifi_sta_mode();
extern FirmwareUpdateView* get_view_firmware_update();
extern PlaybackView*       get_view_playback();
extern PinPadView*         get_view_pin_pad();

#ifdef BUILD_CLOUD_FEATURES
extern CloudUploadView* get_view_cloud_upload();
#endif

TaskHandle_t loopTask_core0_Handle = NULL;
static void  loopTask_core0(void* pvParameters);
bool         show_info_dialog(const char* text, uint16_t next_view);
static bool  show_pairing_success_dialog(const BtManager::PairedDevice& device);
bool         bluetooth_in_recording_state(BtManager::State state);
static void  handle_pending_bluetooth_recording();
static void  handle_pending_bluetooth_pairing();
static void  handle_pending_bluetooth_connect_failed();
static void  handle_bluetooth_recording_failure(const char* message);
#ifdef BUILD_CLOUD_FEATURES
static void handle_pending_cloud_upload_complete();
#endif
static void        handle_pending_ntp_sync_complete();
static void        handle_wifi_connection_waiting();
static void        handle_wifi_station_connected();
static void        handle_firmware_update_on_boot();
static bool        battery_fullish_for_firmware_update();
void               show_wifi_connection_failed(const char* text);
bool               connect_to_bluetooth_host(const bt_host_item_t* host, const char* source);
void               request_bluetooth_disconnect();
bool               show_playback_view(const char* path);
static const char* ntp_error_name(NtpSync::Error error);

FlyGui*                 gui;
#ifdef TEST_BUILD_SCREENSHOT
static GuiDisplay       g_thefly_display(M5.Display);
GuiDisplay&             thefly_display = g_thefly_display;
#else
GuiDisplay&             thefly_display = M5.Display;
#endif
BtHostList*             bt_host_list;
volatile bool           g_pending_bluetooth_recording       = false;
volatile bool           g_pending_bluetooth_disconnect      = false;
volatile bool           g_pending_bluetooth_pairing         = false;
volatile bool           g_bluetooth_connect_waiting         = false;
volatile bool           g_pending_bluetooth_connect_failed  = false;
volatile bool           g_suppress_bluetooth_auto_recording = false;
bool                    g_wifi_connect_waiting              = false;
extern bool             g_nvs_ready;
NtpSync                 g_ntp_sync;
bool                    g_ntp_sync_waiting               = false;
bool                    g_ntp_sync_completion_suppressed = false;
volatile bool           g_pending_ntp_sync_complete      = false;
NtpSync::Result         g_pending_ntp_sync_result        = {};
static uint16_t         g_conn_waiting_return_view_id    = FLYGUI_VIEW_MAIN;
BtManager::PairedDevice g_pending_paired_device          = {};

#ifdef BUILD_CLOUD_FEATURES
CloudUpload         g_cloud_upload;
volatile bool       g_pending_cloud_upload_complete = false;
CloudUpload::Status g_pending_cloud_upload_status   = {};
#endif

void setup()
{
    all_init();

    run_test();

    BattTracker::init();
    BattTracker::shutdownIfNeeded();

    if (reset_was_magic == false)
    {
        show_splash();
        draw_splash_boot_info();
    }

    ExtCodec::init();

    if (!MicroSdCard::begin())
    {
        show_boot_error_f(true, "microSD init failed");
    }

    handle_firmware_update_on_boot();
    WifiManager::clear();

#if BUILD_WITH_SECURITY_LEVEL > 0
    if (!g_nvs_ready || !Aegis::init())
    {
        show_boot_error_f(true, "security subsystem init failed");
    }
#ifndef TEST_MOCK_NVS_FW_SECURED
    if (!Aegis::isNvsEncrypted())
    {
        show_boot_error_f(true, "NVS is not encrypted");
    }
    if (!Aegis::isFwUpdateSecure())
    {
        show_boot_error_f(true, "FW updates are not secured");
    }
#endif
#ifdef TEST_MOCK_PASSWORD
    DBG_LOGD(MAINTAG, "[%u] setting test password...", millis());
    Aegis::setTestTempPassword((const uint8_t*)"123456");
    DBG_LOGD(MAINTAG, "[%u] test password finished setting", millis());
#endif
    if (!Aegis::hasMasterKey())
    {
        show_boot_error_f(true, "no key, please setup a password");
    }
#else
    if (!g_nvs_ready)
    {
        show_boot_error_f(true, "NVS init failed");
    }
#endif

#if BUILD_WITH_SECURITY_LEVEL <= 0
    if (!WifiManager::loadFromMicroSd())
#else
    if (!WifiManager::loadFromNvs())
#endif
    {
        show_boot_error_f(false, "Wi-Fi configuration load failed: %s", WifiManager::lastLoadResultName());
    }
    WifiManager::setOnScanFinished(on_wifi_scan_finished);
    if (gui->currentView() && gui->currentView()->id() == FLYGUI_VIEW_SPLASH)
    {
        draw_splash_boot_info();
    }

    const AudioManager::Hardware audioHardware =
        ExtCodec::available() ? AudioManager::Hardware::ExternalI2SCodec : AudioManager::Hardware::M5StackInternal;
    if (!AudioManager::init(audioHardware))
    {
        show_boot_error_f(true, "AudioManager init failed");
    }

    BtManager::setStateChangedCallback(on_bluetooth_state_changed);
    BtManager::setPairedCallback(on_bluetooth_paired);
    BtManager::setAudioCallbacks(AudioManager::hfp_incoming_audio, AudioManager::hfp_outgoing_audio);
    BtManager::generateLegacyPinFromMac();
    DBG_LOGI(MAINTAG, "Bluetooth legacy pairing PIN: %s", BtManager::generatedLegacyPin());
    if (!BtManager::init(nullptr,
                         AudioManager::hfp_incoming_audio,
                         AudioManager::hfp_outgoing_audio,
                         BtManager::generatedLegacyPin()))
    {
        show_boot_error_f(true, "BluetoothManager init failed");
    }

    bt_host_list = new BtHostList();
#if BUILD_WITH_SECURITY_LEVEL <= 0
    if (!bt_host_list->loadFromMicroSd())
#else
    if (!bt_host_list->loadFromNvs())
#endif
    {
        show_boot_error_f(false, "Bluetooth host list load failed");
    }
    if (!bt_host_list->pruneBonds())
    {
        show_boot_error_f(false, "Bluetooth bond pruning failed: %s", bt_host_list->lastLoadResultName());
    }

#ifdef TEST_BOOT_ERROR_NONFATAL
    show_boot_error_f(false, "test non fatal error");
#endif

#ifdef TEST_BOOT_ERROR_FATAL
    show_boot_error_f(true, "test fatal error");
#endif

    if (!AudioManager::syncExternalCodecRouting())
    {
        show_boot_error_f(false, "SGTL5000 audio routing failed");
    }

    if (gui->currentView() && gui->currentView()->id() == FLYGUI_VIEW_SPLASH)
    {
        draw_splash_idle_prompt();
    }

    if (!gui->currentView())
    {
        if (!gui->showView(FLYGUI_VIEW_MAIN))
        {
            show_boot_error_f(true, "Failed to show main view");
        }
    }

    xTaskCreateUniversal(loopTask_core0,
                         "loopTask_core0",
                         getArduinoLoopTaskStackSize(),
                         NULL,
                         1,
                         &loopTask_core0_Handle,
                         0);
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

    // these functions handle event flags that are boolean
    // and the actions need to happen on this CPU's main loop thread
    handle_pending_bluetooth_pairing();
    handle_pending_bluetooth_connect_failed();
    handle_pending_bluetooth_recording();
#ifdef BUILD_CLOUD_FEATURES
    handle_pending_cloud_upload_complete();
#endif
    handle_pending_ntp_sync_complete();

    // this handles touch events and redraws as needed
    // the critical thing to know is that: these are mostly blocking I2C and SPI calls
    // which is actually why it is shared with `AudioFileRecorder::pump();`, so the SPI calls are not going to overlap
    gui->poll();

    // as the above comment says, this `pump` call can use SPI, so it is in the same thread as the GUI, which uses
    // SPI to draw to the LCD screen
    AudioFileRecorder::pump();
    // internally checks "needsPump"

    if (PlaybackView* playback_view = get_view_playback())
    {
        playback_view->pumpPlayback();
    }

    WifiManager::poll();
    handle_wifi_connection_waiting();

    Hotel::pollCore1();
    Diagnostics::core1Tick();
    Diagnostics::poll();

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
        Diagnostics::core0Tick();
        vTaskDelay(pdMS_TO_TICKS(1));
        // we use vTaskDelay instead of taskYIELD to give other tasks more breathing room
        // other tasks could be Wi-Fi and Bluetooth internal tasks
    }
}

uint16_t conn_waiting_return_view_id()
{
    return g_conn_waiting_return_view_id;
}

void remember_conn_waiting_return_view()
{
    if (!gui->currentView() || gui->currentView()->id() == FLYGUI_VIEW_CONN_WAITING)
    {
        g_conn_waiting_return_view_id = FLYGUI_VIEW_MAIN;
        return;
    }

    g_conn_waiting_return_view_id = gui->currentView()->id();
}

static bool show_conn_waiting_mode(ConnWaitingMode mode, const char* targetName)
{
    ConnWaitingView* view = get_view_conn_waiting();
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
    if (ConnWaitingView* view = get_view_conn_waiting())
    {
        view->configure(CONN_WAITING_WIFI_CONNECTING, targetName);
        view->setCancelCallback(conn_waiting_cancel);
    }
}

#ifdef BUILD_CLOUD_FEATURES
bool show_cloud_upload_view(CloudUpload* uploader, const char* targetName)
{
    CloudUploadView* view = get_view_cloud_upload();
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
    RecordingView* view = get_view_recording();
    if (!gui || !view)
    {
        return false;
    }

    view->configureBluetoothMode();
    return gui->showView(FLYGUI_VIEW_RECORDING);
}

bool promote_recording_view_memo_to_bluetooth()
{
    RecordingView* view = get_view_recording();
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
    RecordingView* view = get_view_recording();
    if (!gui || !view)
    {
        return false;
    }

    const bool started = view->beginMemoRecording();
    return started && gui->showView(FLYGUI_VIEW_RECORDING);
}

bool show_playback_view(const char* path)
{
    PlaybackView* view = get_view_playback();
    if (!gui || !view || !path || path[0] == '\0')
    {
        return false;
    }

    view->configureFile(path);
    return gui->showView(FLYGUI_VIEW_PLAYBACK);
}

bool show_wifi_ap_mode_view()
{
    return gui && gui->showView(FLYGUI_VIEW_AP_MODE);
}

bool show_wifi_sta_mode_view(bool showDismissButton)
{
    WifiStaModeView* view = get_view_wifi_sta_mode();
    if (!gui || !view)
    {
        return false;
    }

    view->configure(showDismissButton);
    return gui->showView(FLYGUI_VIEW_STA_MODE);
}

static void handle_firmware_update_on_boot()
{
    if (!DiskStats::firmwareUpdateFileExists())
    {
        return;
    }

    FirmwareUpdateView* view = get_view_firmware_update();
    if (!gui || !view)
    {
        show_fatal_error_f(true, "Firmware update view unavailable");
        return;
    }

    view->configure(battery_fullish_for_firmware_update());
    if (!gui->showView(FLYGUI_VIEW_FIRMWARE_UPDATE))
    {
        show_fatal_error_f(true, "Firmware update view failed");
        return;
    }

    while (!view->dismissed())
    {
        gui->poll();
        delay(10);
    }

    if (gui->currentView() && gui->currentView()->id() == FLYGUI_VIEW_FIRMWARE_UPDATE)
    {
        gui->showView(FLYGUI_VIEW_MAIN);
    }
}

static bool battery_fullish_for_firmware_update()
{
    return BattTracker::level() == BattTracker::ChargeLevel::high;
}

bool bluetooth_in_recording_state(BtManager::State state)
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
            DBG_LOGW(MAINTAG, "failed to add paired Bluetooth host: %s", bt_host_list->lastLoadResultName());
            show_fatal_error_f(false,
                               "Bluetooth pair saved in NVS, but host list insert failed: %s",
                               bt_host_list->lastLoadResultName());
        }
        else if (!bt_host_list->saveToNvs())
        {
            DBG_LOGW(MAINTAG, "failed to save Bluetooth host list: %s", bt_host_list->lastLoadResultName());
            show_fatal_error_f(false,
                               "Bluetooth pair saved in NVS, but Bluetooth host list save failed: %s",
                               bt_host_list->lastLoadResultName());
        }
    }

    ScrollView* scroll_view = get_view_scroll();
    if (scroll_view)
    {
        scroll_view->populateBluetooth(bt_host_list);
    }

    g_suppress_bluetooth_auto_recording = true;
    const bool pairing_dialog_shown     = show_pairing_success_dialog(g_pending_paired_device);
    if (pairing_dialog_shown)
    {
        haptic_play_done();
    }
    else
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

    if (!bluetooth_in_recording_state(BtManager::state()))
    {
        DBG_LOGE(MAINTAG, "handle_pending_bluetooth_recording called but the state is not in-recording");
        return;
    }

    if (AudioFileRecorder::isRecording())
    {
        if (promote_recording_view_memo_to_bluetooth())
        {
            DBG_LOGI(MAINTAG, "Bluetooth connected while memo recording; promoting active recording view");
            if (!RecordingViewCallbacks::promoteMemoRecordingToBluetooth())
            {
                DBG_LOGW(MAINTAG, "failed to promote memo audio routing for Bluetooth");
            }
        }
        // it's not nice but failing silently is ok here
        return;
    }

    // design policy: playback can be interrupted
    PlaybackView* playback_view = get_view_playback();
    if (playback_view && playback_view->playbackActive())
    {
        playback_view->stopPlayback();
    }

    DBG_LOGI(MAINTAG, "Bluetooth recording trigger accepted at state: %s", BtManager::stateName(BtManager::state()));
    if (!RecordingViewCallbacks::beginBluetoothRecording(AudioFileRecorder::RecordingType::Meeting))
    {
        DBG_LOGE(MAINTAG, "failed to start Bluetooth recording");
        handle_bluetooth_recording_failure("Bluetooth recording start failed");
        return;
    }

    if (!show_recording_view_bluetooth())
    {
        DBG_LOGE(MAINTAG, "failed to show Bluetooth recording view");
        handle_bluetooth_recording_failure("Bluetooth recording view failed");
    }
}

static void handle_bluetooth_recording_failure(const char* message)
{
    g_bluetooth_connect_waiting         = false;
    g_pending_bluetooth_connect_failed  = false;
    g_pending_bluetooth_recording       = false;
    g_suppress_bluetooth_auto_recording = false;

    RecordingViewCallbacks::stopRecording(true);
    error_unexpected_f(FLYGUI_VIEW_MAIN, MAINTAG, "%s", message ? message : "Bluetooth recording failed");
}

static void handle_pending_bluetooth_connect_failed()
{
    if (!g_pending_bluetooth_connect_failed)
    {
        return;
    }
    g_pending_bluetooth_connect_failed = false;

    /*
    this should really never happen
    when it actually did happen, we had a bad build configuration, parts of the HFP inside the ESP-IDF's Bluedroid
    implementation was simply missing
    */

    DBG_LOGW(MAINTAG, "Bluetooth host connection failed before HFP service level connection");

    ScrollView* scroll_view = get_view_scroll();
    if (scroll_view)
    {
        scroll_view->populateBluetooth(bt_host_list);
    }

    error_remote_f(FLYGUI_VIEW_SCROLL, MAINTAG, "Unable to connect to host\nMaybe try connecting from the host.");
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
    const bool succeeded = status.state == CloudUpload::State::Done && status.error == CloudUpload::Error::None;

    ModalDialog* dialog = get_view_modal_dialog();
    if (!dialog || !gui)
    {
        DBG_LOGW(MAINTAG, "cloud upload complete but modal dialog is unavailable; rebooting");
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

    const NtpSync::Result result          = g_pending_ntp_sync_result;
    const bool            suppress_dialog = g_ntp_sync_completion_suppressed;
    g_ntp_sync_waiting                    = false;
    g_ntp_sync_completion_suppressed      = false;

    if (suppress_dialog)
    {
        return;
    }

    ModalDialog* dialog = get_view_modal_dialog();

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
        haptic_play_done();
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
    if (!g_wifi_connect_waiting)
    {
        return;
    }

    switch (WifiManager::status())
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
    DBG_LOGI(MAINTAG,
             "Wi-Fi station connected: ssid=%s ip=%s gateway=%s",
             WifiManager::connectedWifi() ? WifiManager::connectedWifi()->ssid : "",
             WiFi.localIP().toString().c_str(),
             WiFi.gatewayIP().toString().c_str());

    haptic_play_done();

    gui->setWifiActive(true); // signals to the GUI to use a slower polling mode

    if (!WebServer::init())
    {
        show_fatal_error_f(false, "Wi-Fi web server failed to start");
        return;
    }

    ScrollView* scroll_view = get_view_scroll();

    if (!scroll_view->populateCloud())
    {
        show_fatal_error_f(false, "Wi-Fi action list failed");
        return;
    }

    if (!gui->showView(FLYGUI_VIEW_SCROLL))
    {
        show_fatal_error_f(false, "Wi-Fi action view failed");
        return;
    }
}

void show_wifi_connection_failed(const char* text)
{
    g_wifi_connect_waiting = false;

    ScrollView* scroll_view = get_view_scroll();
    scroll_view->populateWifi();

    error_remote_f(FLYGUI_VIEW_SCROLL, MAINTAG, "%s", text ? text : "Wi-Fi connection failed");
}

// triggered from user clicking on a button, either from the main view or from the list
bool connect_to_bluetooth_host(const bt_host_item_t* host, const char* click_source)
{
    if (!host)
    {
        error_usercaused_f(FLYGUI_VIEW_MAIN, MAINTAG, "No Bluetooth host configured");
        return false;
    }

    char bdaddr_text[18] = {};
    format_bdaddr(host->bdaddr, bdaddr_text, sizeof(bdaddr_text));
    DBG_LOGI(MAINTAG,
             "connecting to Bluetooth host from %s: name=%s bdaddr=%s",
             click_source ? click_source : "unknown",
             bt_host_display_name(host),
             bdaddr_text);

    if (bluetooth_in_recording_state(BtManager::state()) && bda_equal(BtManager::connectedMac(), host->bdaddr))
    {
        DBG_LOGI(MAINTAG, "Bluetooth host is already connected; starting recording without a new HFP connection");
        g_bluetooth_connect_waiting        = false;
        g_pending_bluetooth_connect_failed = false;
        g_pending_bluetooth_recording      = true;
        return true;
    }

    g_bluetooth_connect_waiting        = true;
    g_pending_bluetooth_connect_failed = false;

    const BtManager::Result result = BtManager::connectToMac(host->bdaddr);
    if (result != BtManager::Result::Ok)
    {
        g_bluetooth_connect_waiting = false;
        DBG_LOGW(MAINTAG, "Bluetooth connection start failed: %s", BtManager::resultName(result));
        show_fatal_error_f(false, "Bluetooth connection failed: %s", BtManager::resultName(result));
        return false;
    }

    if (!show_conn_waiting_bluetooth(bt_host_display_name(host)))
    {
        g_bluetooth_connect_waiting = false;
        DBG_LOGE(MAINTAG, "failed to show Bluetooth connection waiting view");
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
    ModalDialog* dialog = get_view_modal_dialog();
    dialog->configure(sprite_info_100,
                      SPRITE_INFO_100_BYTES,
                      SPRITE_INFO_100_WIDTH,
                      SPRITE_INFO_100_HEIGHT,
                      text ? text : "",
                      next_view);
    return gui->showView(FLYGUI_VIEW_MODAL_DIALOG);
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

static bool show_pairing_success_dialog(const BtManager::PairedDevice& device)
{
    ModalDialog* dialog = get_view_modal_dialog();
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
