#include "thefly_common.h"
#include "main_callbacks.h"

#include <Arduino.h>
#include <WiFi.h>
#include "BluetoothManager.h"
#include "BtHostList.h"
#include "ConnWaitingView.h"
#include "DiskStats.h"
#include "FlyGui.h"
#include "MainScreenView.h"
#include "NtpSync.h"
#include "PinPadView.h"
#include "ScrollView/ScrollView.h"
#include "WebServer.h"
#include "WifiManager.h"
#include "dbg_log.h"
#include "esp_system.h"
#include "utilfuncs.h"
#include <stdio.h>
#include <string.h>

#ifdef BUILD_CLOUD_FEATURES
#include "CloudUpload.h"
#endif

constexpr const char* MAINTAG = "main_callbacks.cpp";

extern FlyGui*                 gui;
extern BtHostList*             bt_host_list;
extern WifiManager*            wifi_manager;
extern volatile bool           g_pending_bluetooth_recording;
extern volatile bool           g_pending_bluetooth_pairing;
extern volatile bool           g_bluetooth_connect_waiting;
extern volatile bool           g_pending_bluetooth_connect_failed;
extern volatile bool           g_suppress_bluetooth_auto_recording;
extern bool                    g_wifi_connect_waiting;
extern NtpSync                 g_ntp_sync;
extern bool                    g_ntp_sync_waiting;
extern bool                    g_ntp_sync_completion_suppressed;
extern volatile bool           g_pending_ntp_sync_complete;
extern NtpSync::Result         g_pending_ntp_sync_result;
extern BtManager::PairedDevice g_pending_paired_device;

extern ScrollView*      get_view_scroll();
extern PinPadView*      get_view_pin_pad();
extern uint16_t         conn_waiting_return_view_id();
extern ConnWaitingView* get_view_conn_waiting();
extern MainScreenView*  get_view_main_screen();
extern bool             show_conn_waiting_bluetooth(const char* targetName);
extern bool             show_conn_waiting_bluetooth_pairing();
extern bool             show_conn_waiting_wifi_connecting(const char* targetName);
extern bool             show_conn_waiting_wifi_scanning(const char* targetName);
extern bool             show_conn_waiting_ntp_sync(const char* targetName);
extern void             update_conn_waiting_wifi_target(const char* targetName);
extern bool             show_recording_view_memo();
extern bool             show_wifi_ap_mode_view();
extern bool             show_wifi_sta_mode_view(bool showDismissButton);
extern bool             show_playback_view(const char* path);
extern bool             show_info_dialog(const char* text, uint16_t next_view);
extern bool             bluetooth_in_recording_state(BtManager::State state);
extern bool             connect_to_bluetooth_host(const bt_host_item_t* host, const char* source);
extern void             request_bluetooth_disconnect();
extern void             show_wifi_connection_failed(const char* text);
extern void             show_fatal_error_f(bool fatal, const char* format, ...);

#ifdef BUILD_CLOUD_FEATURES
extern CloudUpload         g_cloud_upload;
extern volatile bool       g_pending_cloud_upload_complete;
extern CloudUpload::Status g_pending_cloud_upload_status;
extern bool                show_cloud_upload_view(CloudUpload* uploader, const char* targetName);
#endif

// called from with BtManager
void on_bluetooth_state_changed(BtManager::State state)
{
    DBG_LOGI(MAINTAG, "Bluetooth state callback: %s", BtManager::stateName(state));
    if (bluetooth_in_recording_state(state))
    {
        g_bluetooth_connect_waiting        = false;
        g_pending_bluetooth_connect_failed = false;
        if (g_suppress_bluetooth_auto_recording)
        {
            DBG_LOGI(MAINTAG,
                     "Bluetooth recording auto-start suppressed during pairing profile setup: %s",
                     BtManager::stateName(state));
            return;
        }
        DBG_LOGI(MAINTAG, "queueing Bluetooth recording start from state: %s", BtManager::stateName(state));
        g_pending_bluetooth_recording = true; // signals handle_pending_bluetooth_recording()
    }
    else if (state == BtManager::State::Idle && g_bluetooth_connect_waiting)
    {
        g_bluetooth_connect_waiting        = false;
        g_pending_bluetooth_connect_failed = true; // signals handle_pending_bluetooth_connect_failed()
    }
}

// called from BtManager
void on_bluetooth_paired(const BtManager::PairedDevice& device)
{
    g_suppress_bluetooth_auto_recording = true;
    g_pending_paired_device             = device;
    g_pending_bluetooth_pairing         = true; // signals handle_pending_bluetooth_pairing()
}

// called from MainScreenView
void onclick_main_bluetooth(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "main screen bluetooth selected");
    ScrollView* scroll_view = get_view_scroll();
    if (scroll_view && gui)
    {
        scroll_view->populateBluetooth(bt_host_list);
        gui->showView(FLYGUI_VIEW_SCROLL);
    }
}

static void show_system_info_dialog(uint16_t next_view)
{
    // Fade quickly as feedback because the analysis calls can take a moment.
    FlyGui::quickScreenFade();

    char tail_text[32] = {0};
#if BUILD_WITH_SECURITY_LEVEL == 2
    uint32_t code = 0;
    if (Aegis::tamperEvidenceCode(code))
    {
        snprintf(tail_text, sizeof(tail_text), "\nTamper: %04lX", static_cast<unsigned long>((code >> 16) & 0xFFFF));
    }
#endif

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
#ifdef BUILD_CLOUD_FEATURES
    if (disk_ok && rec_ok)
    {
        snprintf(text,
                 sizeof(text),
                 "Disk: %s/%s free (%u%%)\nFiles: %lu total, %lu waiting\nLast upload: %s\nLatest: %s%s",
                 free_text,
                 total_text,
                 static_cast<unsigned>(DiskStats::freeDiskSpacePercent()),
                 static_cast<unsigned long>(DiskStats::totalRecFilesStored()),
                 static_cast<unsigned long>(DiskStats::totalRecFilesNotUploaded()),
                 last_upload,
                 latest_file,
                 tail_text);
    }
    else
    {
        snprintf(text,
                 sizeof(text),
                 "System Info\nDisk: unavailable\nREC scan: unavailable\nLast upload: %s\nLatest: %s%s",
                 last_upload,
                 latest_file,
                 tail_text);
    }
#else
    if (disk_ok && rec_ok)
    {
        snprintf(text,
                 sizeof(text),
                 "Disk: %s/%s free (%u%%)\nFiles: %lu total\nLatest: %s%s",
                 free_text,
                 total_text,
                 static_cast<unsigned>(DiskStats::freeDiskSpacePercent()),
                 static_cast<unsigned long>(DiskStats::totalRecFilesStored()),
                 latest_file,
                 tail_text);
    }
    else
    {
        snprintf(text,
                 sizeof(text),
                 "System Info\nDisk: unavailable\nREC scan: unavailable\nLatest: %s%s",
                 latest_file,
                 tail_text);
    }
#endif

    show_info_dialog(text, next_view);
}

// called from MainScreenView
void onclick_main_info(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "main screen info selected");
    show_system_info_dialog(FLYGUI_VIEW_MAIN);
}

#if BUILD_WITH_SECURITY_LEVEL == 1
static void on_file_list_pin_success()
{
    if (gui)
    {
        gui->showView(FLYGUI_VIEW_SCROLL);
    }
}

static void on_file_list_pin_failed(uint32_t failedAttempts)
{
    DBG_LOGW(MAINTAG, "file list PIN failed attempt: %lu", static_cast<unsigned long>(failedAttempts));
}

static void on_file_list_pin_exit()
{
    if (gui)
    {
        gui->showView(FLYGUI_VIEW_MAIN);
    }
}
#endif

// called from MainScreenView
void onclick_main_files(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "main screen files selected");
    ScrollView* scroll_view = get_view_scroll();
    if (scroll_view && gui)
    {
        scroll_view->populateFiles();
#if BUILD_WITH_SECURITY_LEVEL == 1
        PinPadView* pin_pad = get_view_pin_pad();
        if (!pin_pad)
        {
            show_fatal_error_f(true, "PIN pad unavailable; file list access denied");
            return;
        }

        pin_pad->configure(on_file_list_pin_success, on_file_list_pin_failed, on_file_list_pin_exit);
        if (gui->showView(FLYGUI_VIEW_PIN_PAD))
        {
            return;
        }

        show_fatal_error_f(true, "PIN pad view failed; file list access denied");
        return;
#endif
        gui->showView(FLYGUI_VIEW_SCROLL);
    }
}

// called from MainScreenView
void onclick_main_wifi(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "main screen wifi selected");
    ScrollView* scroll_view = get_view_scroll();
    if (scroll_view && gui)
    {
        scroll_view->populateWifi(wifi_manager);
        gui->showView(FLYGUI_VIEW_SCROLL);
    }
}

// called from MainScreenView
void onclick_main_memo(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "main screen memo selected");
    if (MainScreenView* view = get_view_main_screen())
    {
        view->showMemoStartingFeedback();
    }
    if (!show_recording_view_memo())
    {
        DBG_LOGE(MAINTAG, "failed to start memo recording view");
    }
}

// called from MainScreenView
void onclick_main_smartphone(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "main screen smartphone selected");
    if (!bt_host_list || !connect_to_bluetooth_host(bt_host_list->getFirstPhone(), "smartphone button"))
    {
        DBG_LOGW(MAINTAG, "no smartphone Bluetooth host available");
    }
}

// called from MainScreenView
void onclick_main_laptop(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "main screen laptop selected");
    if (!bt_host_list || !connect_to_bluetooth_host(bt_host_list->getFirstLaptop(), "laptop button"))
    {
        DBG_LOGW(MAINTAG, "no laptop Bluetooth host available");
    }
}

// when user clicks the cancel button in a connection waiting view
void conn_waiting_cancel(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "connection waiting cancel selected");
    if (g_wifi_connect_waiting)
    {
        g_wifi_connect_waiting = false;
        wifi_manager->disconnect();
        const uint16_t return_view = conn_waiting_return_view_id();
        if (!gui->showView(return_view))
        {
            gui->showView(FLYGUI_VIEW_SCROLL);
        }
        return;
    }

    if (g_ntp_sync_waiting)
    {
        g_ntp_sync_waiting               = false;
        g_ntp_sync_completion_suppressed = true;
        g_ntp_sync.cancel();
        const uint16_t return_view = conn_waiting_return_view_id();
        if (!gui->showView(return_view))
        {
            gui->showView(FLYGUI_VIEW_SCROLL);
        }
        return;
    }

    ConnWaitingView* conn_waiting_view = get_view_conn_waiting();
    const bool       bluetooth_waiting =
        conn_waiting_view && (conn_waiting_view->mode() == CONN_WAITING_BLUETOOTH_CONNECTING ||
                              conn_waiting_view->mode() == CONN_WAITING_BLUETOOTH_PAIRING);
    if (bluetooth_waiting)
    {
        g_bluetooth_connect_waiting         = false;
        g_pending_bluetooth_connect_failed  = false;
        g_suppress_bluetooth_auto_recording = false;
        const uint16_t return_view          = conn_waiting_return_view_id();
        if (!gui->showView(return_view))
        {
            gui->showView(FLYGUI_VIEW_MAIN);
        }
        request_bluetooth_disconnect();
        return;
    }

    DBG_LOGW(MAINTAG, "connection waiting cancel ignored: no cancellable wait is active");
}

void onclick_scroll_exit(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "scroll view exit selected");
    ScrollView* scroll_view = get_view_scroll();

    // by policy, if Wi-Fi has been used, we do not allow restarting of Bluetooth, so we do a reboot
    if (scroll_view && scroll_view->isCloudContext())
    {
        DBG_LOGI(MAINTAG, "cloud scroll exit selected; rebooting");
        delay(50);
        esp_restart();
        return;
    }

    // by policy, if Wi-Fi has been used, we do not allow restarting of Bluetooth, so we do a reboot
    if (scroll_view && scroll_view->isWifiContext() && wifi_manager && wifi_manager->wifiHasStarted())
    {
        thefly_display.fillScreen(TFT_BLACK);
        DBG_LOGI(MAINTAG, "Wi-Fi scroll exit selected after Wi-Fi start; rebooting");
        delay(50);
        esp_restart();
        return;
    }

    gui->showView(FLYGUI_VIEW_MAIN);
}

// called when user clicks on a host in the ScrollView
void onclick_bluetooth_host(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "scroll bluetooth host selected: index=%ld", static_cast<long>(value));
    if (value < 0 || !bt_host_list ||
        !connect_to_bluetooth_host(bt_host_list->get(static_cast<size_t>(value)), "Bluetooth host list"))
    {
        DBG_LOGW(MAINTAG, "could not start Bluetooth host connection for index=%ld", static_cast<long>(value));
    }
}

// called when user chooses to pair in the ScrollView
void onclick_bluetooth_pair(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "scroll bluetooth pair selected: task=%ld", static_cast<long>(value));

    g_suppress_bluetooth_auto_recording = true;
    const BtManager::Result result      = BtManager::startPairing();
    if (result != BtManager::Result::Ok)
    {
        g_suppress_bluetooth_auto_recording = false;
        DBG_LOGW(MAINTAG, "Bluetooth pairing start failed: %s", BtManager::resultName(result));
        show_fatal_error_f(false, "Bluetooth pairing failed: %s", BtManager::resultName(result));
        return;
    }

    if (!show_conn_waiting_bluetooth_pairing())
    {
        DBG_LOGE(MAINTAG, "failed to show Bluetooth pairing waiting view");
        BtManager::disconnect();
        g_suppress_bluetooth_auto_recording = false;
        show_fatal_error_f(false, "Bluetooth pairing view failed");
    }
}

// called when user chooses to scan in the ScrollView
void onclick_wifi_scan_and_connect(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "scroll wifi scan/connect selected: task=%ld", static_cast<long>(value));
    if (!wifi_manager)
    {
        DBG_LOGW(MAINTAG, "cannot scan/connect: Wi-Fi manager is not available");
        return;
    }

    if (!wifi_manager->scanAndConnect())
    {
        DBG_LOGW(MAINTAG, "could not start Wi-Fi scan/connect: %s", wifi_manager->statusName());
        show_wifi_connection_failed("Wi-Fi scan failed to start");
        return;
    }

    g_wifi_connect_waiting = true;
    if (!show_conn_waiting_wifi_scanning("known Wi-Fi"))
    {
        g_wifi_connect_waiting = false;
        wifi_manager->disconnect();
        show_fatal_error_f(false, "Wi-Fi connection view failed");
    }
}

// called when user clicks on a router in the ScrollView
void onclick_wifi_station(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "scroll wifi station selected: index=%ld", static_cast<long>(value));
    if (value < 0 || !wifi_manager)
    {
        show_fatal_error_f(false, "Wi-Fi station selection is invalid");
        return;
    }

    const wifi_item_t* station = wifi_manager->station(static_cast<size_t>(value));
    if (!station)
    {
        show_fatal_error_f(false, "Wi-Fi station index is invalid");
        return;
    }

    if (!wifi_manager->connectToHotspot(station))
    {
        DBG_LOGW(MAINTAG, "could not start Wi-Fi station connection: %s", wifi_manager->statusName());
        show_wifi_connection_failed("Wi-Fi connection failed to start");
        return;
    }

    g_wifi_connect_waiting = true;
    if (!show_conn_waiting_wifi_connecting(station->ssid))
    {
        g_wifi_connect_waiting = false;
        wifi_manager->disconnect();
        show_fatal_error_f(false, "Wi-Fi connection view failed");
    }
}

// called when user clicks on Wi-Fi AP in the ScrollView
void onclick_wifi_ap(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "scroll wifi ap selected: index=%ld", static_cast<long>(value));
    if (!wifi_manager)
    {
        show_fatal_error_f(false, "Wi-Fi AP is unavailable");
        return;
    }

    bool started = false;
    if (value == SCROLL_TASK_WIFI_GENERATED_AP)
    {
        started = wifi_manager->startGeneratedSoftAp();
    }
    else if (value >= 0)
    {
        const wifi_item_t* access_point = wifi_manager->accessPoint(static_cast<size_t>(value));
        if (!access_point)
        {
            show_fatal_error_f(false, "Wi-Fi AP index is invalid");
            return;
        }
        started = wifi_manager->startSoftAp(access_point);
    }
    else
    {
        show_fatal_error_f(false, "Wi-Fi AP selection is invalid");
        return;
    }

    if (!started)
    {
        show_fatal_error_f(false, "Wi-Fi AP start failed: %s", wifi_manager->statusName());
        return;
    }

    if (!WebServer::init())
    {
        show_fatal_error_f(false, "Wi-Fi web server failed to start");
        return;
    }

    if (!show_wifi_ap_mode_view())
    {
        show_fatal_error_f(false, "Wi-Fi AP view failed");
    }
}

#ifdef BUILD_CLOUD_FEATURES
// this is when the user tries to start cloud uploading
void onclick_cloud_upload(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "scroll cloud upload selected: index=%ld", static_cast<long>(value));

    if (value < 0 || !wifi_manager)
    {
        show_fatal_error_f(false, "Cloud upload selection is invalid");
        return;
    }

    const cloud_item_t* endpoint = wifi_manager->cloudEndpoint(static_cast<size_t>(value));
    if (!endpoint)
    {
        show_fatal_error_f(false, "Cloud upload endpoint is invalid");
        return;
    }

    g_cloud_upload.setOnCompleteCallback(on_cloud_upload_complete);
    if (!g_cloud_upload.start(endpoint))
    {
        const CloudUpload::Status status = g_cloud_upload.status();
        error_unexpected_f(FLYGUI_VIEW_SCROLL,
                           MAINTAG,
                           "%s",
                           status.message[0] != '\0' ? status.message : "Cloud upload failed to start");
        return;
    }

    if (!show_cloud_upload_view(&g_cloud_upload, endpoint->url))
    {
        g_cloud_upload.cancel();
        show_fatal_error_f(false, "Cloud upload view failed");
    }
}

void on_cloud_upload_complete(const CloudUpload::Status& status)
{
    g_pending_cloud_upload_status   = status; // payload consumed by handle_pending_cloud_upload_complete()
    g_pending_cloud_upload_complete = true;   // signals handle_pending_cloud_upload_complete()
}

void on_cloud_upload_dialog_dismissed()
{
    DBG_LOGI(MAINTAG, "cloud upload confirmation dismissed; rebooting");
    delay(50);
    esp_restart();
}

// this is when the user cancels during an upload session
void cloud_upload_cancel(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "cloud upload cancel selected");
    g_cloud_upload.cancel();
}
#endif

// from within the ScrollView after Wi-Fi connection is established
void onclick_ntp_sync(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "scroll ntp sync selected: task=%ld", static_cast<long>(value));

    if (!wifi_manager)
    {
        error_unexpected_f(FLYGUI_VIEW_SCROLL, MAINTAG, "NTP sync unavailable");
        return;
    }

    const char* primary_server       = wifi_manager->ntpServer(0);
    g_pending_ntp_sync_complete      = false;
    g_ntp_sync_completion_suppressed = false;
    g_ntp_sync_waiting               = true;
    g_ntp_sync.setOnCompleteCallback(on_ntp_sync_complete);
    if (!g_ntp_sync.start(*wifi_manager, NtpSync::kDefaultTimeoutMs, true))
    {
        g_ntp_sync_waiting = false;
        error_unexpected_f(FLYGUI_VIEW_SCROLL, MAINTAG, "NTP sync failed\n%s", g_ntp_sync.errorName());
        return;
    }

    if (!show_conn_waiting_ntp_sync(primary_server && primary_server[0] != '\0' ? primary_server : "NTP server"))
    {
        g_ntp_sync_waiting               = false;
        g_ntp_sync_completion_suppressed = true; // read by handle_pending_ntp_sync_complete()
        g_ntp_sync.cancel();
        show_fatal_error_f(false, "NTP sync view failed"); // TODO: change to use error_unexpected_f, indicates a
                                                           // showView problem or gui is null or something
    }
}

// from within ScrollView in the Bluetooth context
void onclick_bt_show_info(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "scroll bluetooth info selected: task=%ld", static_cast<long>(value));

    esp_bd_addr_t bdaddr          = {};
    char          bdaddr_text[18] = "unknown";
    if (BtManager::localBdaddr(bdaddr))
    {
        format_bdaddr(bdaddr, bdaddr_text, sizeof(bdaddr_text));
    }

    char text[160];
    snprintf(text, sizeof(text), "Bluetooth\nName: %s\nBDADDR: %s", BtManager::localDeviceName(), bdaddr_text);
    show_info_dialog(text, FLYGUI_VIEW_SCROLL);
}

// from within ScrollView in the Wi-Fi context
void onclick_wifi_show_info(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "scroll wifi info selected: task=%ld", static_cast<long>(value));

    if (wifi_manager && wifi_manager->status() == WifiManager::Status::StationConnected)
    {
        if (!show_wifi_sta_mode_view(true))
        {
            show_fatal_error_f(false, "Wi-Fi station view failed");
        }
        return;
    }

    // the much richer station mode view should never fail
    // the code below may never execute under normal circumstances
    // but there is the chance that the Wi-Fi station disconnected at some point before the user clicks the button

    const WifiManager::Status status               = wifi_manager ? wifi_manager->status() : WifiManager::Status::Idle;
    const bool                is_ap                = status == WifiManager::Status::AccessPoint;
    const bool                is_station_connected = status == WifiManager::Status::StationConnected;
    const bool                is_connected         = is_ap || is_station_connected;
    const wifi_item_t*        item =
        is_connected && wifi_manager
            ? (wifi_manager->connectedWifi() ? wifi_manager->connectedWifi() : wifi_manager->activeWifi())
            : nullptr;
    const char*     ssid    = item && item->ssid ? item->ssid : "";
    const IPAddress ip      = is_ap ? wifi_manager->softApIp() : (is_station_connected ? WiFi.localIP() : IPAddress());
    const String    ip_text = ip.toString();

    char text[192];
    if (is_ap && item)
    {
        snprintf(text, sizeof(text), "Wi-Fi\nSSID: %s\nIP: %s", ssid, ip_text.c_str());
    }
    else
    {
        snprintf(text,
                 sizeof(text),
                 "Wi-Fi\nSSID: %s\nIP: %s",
                 is_connected && ssid[0] != '\0' ? ssid : "(not connected)",
                 ip_text.c_str());
    }

    show_info_dialog(text, FLYGUI_VIEW_SCROLL);
}

// called from a ScrollView for file list
void onclick_file_playable(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "scroll playable file selected: index=%ld", static_cast<long>(value));
    ScrollView* scroll_view = get_view_scroll();
    const char* file_name   = scroll_view ? scroll_view->selectedItemLabel() : "";
    if (!file_name || file_name[0] == '\0')
    {
        error_unexpected_f(FLYGUI_VIEW_SCROLL, MAINTAG, "Playback file is unavailable");
        return;
    }

    char path[96] = {};
    if (file_name[0] == '/')
    {
        strlcpy(path, file_name, sizeof(path));
    }
    else
    {
        snprintf(path, sizeof(path), "/%s", file_name);
    }

    if (!show_playback_view(path))
    {
        error_unexpected_f(FLYGUI_VIEW_SCROLL, MAINTAG, "Unable to open playback view");
    }
}

// called from a ScrollView for file list
void onclick_filelist_show_info(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    DBG_LOGI(MAINTAG, "scroll file list info selected: task=%ld", static_cast<long>(value));
    show_system_info_dialog(FLYGUI_VIEW_SCROLL);
}

void on_pairing_success_dialog_dismissed()
{
    /*
    there's a quirk with how pairing works
    the host really likes to immediately establish a working connection
    immediately after pairing
    but... the policy of this whole project is that: an incoming connection always starts a recording session
    which can get super annoying for just setting up the device
    however, if we deny the connection, the phone freaks out about the device not working
    so the work around is, we allow the connection but deny the recording, only for right after pairing success
    and then we reboot the device to fake a disconnection, this will refresh the internal BtHostList and such
    */
    DBG_LOGI(MAINTAG, "pairing confirmation dismissed; rebooting to restart Bluetooth cleanly");
    g_suppress_bluetooth_auto_recording = false;
    Serial.flush();
    delay(100);
    esp_restart();
}

void on_ntp_sync_complete(const NtpSync::Result& result)
{
    g_pending_ntp_sync_result   = result;
    g_pending_ntp_sync_complete = true; // signals handle_pending_ntp_sync_complete()
}

void on_wifi_scan_finished(const wifi_item_t* item)
{
    if (!wifi_manager)
    {
        show_fatal_error_f(true, "on_wifi_scan_finished has no wifi_manager");
        return;
    }

    if (!item)
    {
        DBG_LOGW(MAINTAG, "Wi-Fi scan/connect did not find a configured station");
        show_fatal_error_f(false, "on_wifi_scan_finished has no item");
        return;
    }

    DBG_LOGI(MAINTAG, "Wi-Fi scan/connect found \"%s\", starting connection", item->ssid ? item->ssid : "");
    update_conn_waiting_wifi_target(item->ssid);
    if (!wifi_manager->connectToHotspot(item))
    {
        DBG_LOGW(MAINTAG, "could not connect to scanned Wi-Fi station: %s", wifi_manager->statusName());
        show_wifi_connection_failed("Wi-Fi connection failed to start");
    }
}
