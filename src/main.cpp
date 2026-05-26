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
extern bool show_conn_waiting_bluetooth(const char* targetName);
extern bool show_conn_waiting_bluetooth_pairing();
extern uint16_t conn_waiting_return_view_id();
extern bool show_recording_view_bluetooth();
extern bool show_recording_view_memo();
extern bool promote_recording_view_memo_to_bluetooth();
extern ScrollView* get_scroll_view();
extern ModalDialog* get_modal_dialog();
extern void show_main_memo_starting_feedback();

void onclick_scroll_exit(uint32_t pressDurationMs);
void onclick_bluetooth_host(int32_t value, uint32_t pressDurationMs);
void onclick_bluetooth_pair(int32_t value, uint32_t pressDurationMs);
void onclick_wifi_scan_and_connect(int32_t value, uint32_t pressDurationMs);
void onclick_wifi_station(int32_t value, uint32_t pressDurationMs);
void onclick_wifi_ap(int32_t value, uint32_t pressDurationMs);
void onclick_cloud_upload(int32_t value, uint32_t pressDurationMs);
void onclick_ntp_sync(int32_t value, uint32_t pressDurationMs);
void onclick_bt_show_info(int32_t value, uint32_t pressDurationMs);
void onclick_wifi_show_info(int32_t value, uint32_t pressDurationMs);

TaskHandle_t loopTask_core0_Handle = NULL;
static void  loopTask_core0(void* pvParameters);
static bool  show_info_dialog(const char* text, uint16_t next_view);
static bool  show_error_dialog(const char* text, uint16_t next_view);
static bool  show_pairing_success_dialog(const BtManager::PairedDevice& device);
static void  on_pairing_success_dialog_dismissed();
static bool  bluetooth_recording_state(BtManager::State state);
static void  on_bluetooth_state_changed(BtManager::State state);
static void  on_bluetooth_paired(const BtManager::PairedDevice& device);
static void  handle_pending_bluetooth_recording();
static void  handle_pending_bluetooth_pairing();
static void  handle_pending_bluetooth_connect_failed();
static bool  connect_to_bluetooth_host(const bt_host_item_t* host, const char* source);
static void  on_wifi_scan_finished(const wifi_item_t* item);
static void  request_bluetooth_disconnect();

FlyGui* gui;
M5GFX& thefly_display = M5.Display;
BtHostList* bt_host_list;
WifiManager* wifi_manager;
static volatile bool g_pending_bluetooth_recording = false;
static volatile bool g_pending_bluetooth_disconnect = false;
static volatile bool g_pending_bluetooth_pairing = false;
static volatile bool g_bluetooth_connect_waiting = false;
static volatile bool g_pending_bluetooth_connect_failed = false;
static volatile bool g_suppress_bluetooth_auto_recording = false;
static BtManager::PairedDevice g_pending_paired_device = {};

void setup()
{
    all_init();

    #ifdef RUN_BRINGUP_TEST
    run_test();
    #endif

    if (reset_was_magic == false) {
        show_splash();
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

    if (!AudioManager::init())
    {
        show_fatal_error_f(true, "AudioManager init failed");
    }

    BtManager::setStateChangedCallback(on_bluetooth_state_changed);
    BtManager::setPairedCallback(on_bluetooth_paired);
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
    handle_pending_bluetooth_pairing();
    handle_pending_bluetooth_connect_failed();
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

static bool bluetooth_recording_state(BtManager::State state)
{
    return state == BtManager::State::Connected || state == BtManager::State::AudioAvailable;
}

static void on_bluetooth_state_changed(BtManager::State state)
{
    ESP_LOGI(MAINTAG, "Bluetooth state callback: %s", BtManager::stateName(state));
    if (bluetooth_recording_state(state))
    {
        g_bluetooth_connect_waiting = false;
        g_pending_bluetooth_connect_failed = false;
        if (g_suppress_bluetooth_auto_recording)
        {
            ESP_LOGI(MAINTAG, "Bluetooth recording auto-start suppressed during pairing profile setup: %s", BtManager::stateName(state));
            return;
        }
        ESP_LOGI(MAINTAG, "queueing Bluetooth recording start from state: %s", BtManager::stateName(state));
        g_pending_bluetooth_recording = true;
    }
    else if (state == BtManager::State::Idle && g_bluetooth_connect_waiting)
    {
        g_bluetooth_connect_waiting = false;
        g_pending_bluetooth_connect_failed = true;
    }
}

static void on_bluetooth_paired(const BtManager::PairedDevice& device)
{
    g_pending_paired_device = device;
    g_pending_bluetooth_pairing = true;
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

static bool connect_to_bluetooth_host(const bt_host_item_t* host, const char* source)
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

void onclick_main_bluetooth(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "main screen bluetooth selected");
    ScrollView* scroll_view = get_scroll_view();
    if (scroll_view && gui)
    {
        scroll_view->populateBluetooth(bt_host_list);
        gui->showView(FLYGUI_VIEW_SCROLL);
    }
}

void onclick_main_info(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
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

void onclick_main_wifi(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "main screen wifi selected");
    ScrollView* scroll_view = get_scroll_view();
    if (scroll_view && gui)
    {
        scroll_view->populateWifi(wifi_manager);
        gui->showView(FLYGUI_VIEW_SCROLL);
    }
}

void onclick_main_memo(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "main screen memo selected");
    show_main_memo_starting_feedback();
    if (!show_recording_view_memo())
    {
        ESP_LOGE(MAINTAG, "failed to start memo recording view");
    }
}

void onclick_main_smartphone(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "main screen smartphone selected");
    if (!bt_host_list || !connect_to_bluetooth_host(bt_host_list->getFirstPhone(), "smartphone button"))
    {
        ESP_LOGW(MAINTAG, "no smartphone Bluetooth host available");
    }
}

void onclick_main_laptop(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "main screen laptop selected");
    if (!bt_host_list || !connect_to_bluetooth_host(bt_host_list->getFirstLaptop(), "laptop button"))
    {
        ESP_LOGW(MAINTAG, "no laptop Bluetooth host available");
    }
}

void conn_waiting_cancel(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "connection waiting cancel selected");
    g_bluetooth_connect_waiting = false;
    g_pending_bluetooth_connect_failed = false;
    if (gui)
    {
        const uint16_t return_view = conn_waiting_return_view_id();
        if (!gui->showView(return_view))
        {
            gui->showView(FLYGUI_VIEW_MAIN);
        }
    }
    request_bluetooth_disconnect();
}

static void request_bluetooth_disconnect()
{
    g_pending_bluetooth_disconnect = true;
}

void onclick_scroll_exit(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "scroll view exit selected");
    if (gui)
    {
        gui->showView(FLYGUI_VIEW_MAIN);
    }
}

void onclick_bluetooth_host(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "scroll bluetooth host selected: index=%ld", static_cast<long>(value));
    if (value < 0 || !bt_host_list || !connect_to_bluetooth_host(bt_host_list->get(static_cast<size_t>(value)), "Bluetooth host list"))
    {
        ESP_LOGW(MAINTAG, "could not start Bluetooth host connection for index=%ld", static_cast<long>(value));
    }
}

void onclick_bluetooth_pair(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "scroll bluetooth pair selected: task=%ld", static_cast<long>(value));

    const BtManager::Result result = BtManager::startPairing();
    if (result != BtManager::Result::Ok)
    {
        ESP_LOGW(MAINTAG, "Bluetooth pairing start failed: %s", BtManager::resultName(result));
        show_fatal_error_f(false, "Bluetooth pairing failed: %s", BtManager::resultName(result));
        return;
    }

    if (!show_conn_waiting_bluetooth_pairing())
    {
        ESP_LOGE(MAINTAG, "failed to show Bluetooth pairing waiting view");
        BtManager::disconnect();
        show_fatal_error_f(false, "Bluetooth pairing view failed");
    }
}

void onclick_wifi_scan_and_connect(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
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

void onclick_wifi_station(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "scroll wifi station selected: index=%ld", static_cast<long>(value));
}

void onclick_wifi_ap(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "scroll wifi ap selected: index=%ld", static_cast<long>(value));
}

void onclick_cloud_upload(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "scroll cloud upload selected: index=%ld", static_cast<long>(value));
}

void onclick_ntp_sync(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "scroll ntp sync selected: task=%ld", static_cast<long>(value));
}

void onclick_bt_show_info(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
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

void onclick_wifi_show_info(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
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

static bool show_error_dialog(const char* text, uint16_t next_view)
{
    ModalDialog* dialog = get_modal_dialog();
    if (!dialog || !gui)
    {
        return false;
    }

    dialog->configure(sprit_warning_100,
                      SPRIT_WARNING_100_BYTES,
                      SPRIT_WARNING_100_WIDTH,
                      SPRIT_WARNING_100_HEIGHT,
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

    dialog->configure(sprit_thumbsup_100,
                      SPRIT_THUMBSUP_100_BYTES,
                      SPRIT_THUMBSUP_100_WIDTH,
                      SPRIT_THUMBSUP_100_HEIGHT,
                      text,
                      FLYGUI_VIEW_SCROLL,
                      on_pairing_success_dialog_dismissed);
    return gui->showView(FLYGUI_VIEW_MODAL_DIALOG);
}

static void on_pairing_success_dialog_dismissed()
{
    ESP_LOGI(MAINTAG, "pairing confirmation dismissed; Bluetooth auto-record can resume");
    g_suppress_bluetooth_auto_recording = false;
    if (bluetooth_recording_state(BtManager::state()))
    {
        ESP_LOGI(MAINTAG, "Bluetooth was connected while pairing confirmation was shown; queueing recording start");
        g_pending_bluetooth_recording = true;
    }
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
