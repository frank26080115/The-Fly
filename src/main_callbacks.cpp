#include "thefly_common.h"
#include "main_callbacks.h"

#include <Arduino.h>
#include <WiFi.h>
#include "BluetoothManager.h"
#include "BtHostList.h"
#include "DiskStats.h"
#include "FlyGui.h"
#include "ScrollView/ScrollView.h"
#include "WebServer.h"
#include "WifiManager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "utilfuncs.h"
#include <stdio.h>

#ifdef BUILD_CLOUD_FEATURES
#include "CloudUpload.h"
#endif

constexpr const char* MAINTAG = "main_callbacks.cpp";

extern FlyGui* gui;
extern BtHostList* bt_host_list;
extern WifiManager* wifi_manager;
extern volatile bool g_pending_bluetooth_recording;
extern volatile bool g_pending_bluetooth_pairing;
extern volatile bool g_bluetooth_connect_waiting;
extern volatile bool g_pending_bluetooth_connect_failed;
extern volatile bool g_suppress_bluetooth_auto_recording;
extern bool g_wifi_connect_waiting;
extern BtManager::PairedDevice g_pending_paired_device;

extern ScrollView* get_scroll_view();
extern uint16_t conn_waiting_return_view_id();
extern void show_main_memo_starting_feedback();
extern bool show_conn_waiting_bluetooth(const char* targetName);
extern bool show_conn_waiting_bluetooth_pairing();
extern bool show_conn_waiting_wifi_connecting(const char* targetName);
extern bool show_conn_waiting_wifi_scanning(const char* targetName);
extern void update_conn_waiting_wifi_target(const char* targetName);
extern bool show_recording_view_memo();
extern bool show_wifi_ap_mode_view();
extern bool show_wifi_sta_mode_view(bool showDismissButton);
extern bool show_info_dialog(const char* text, uint16_t next_view);
extern bool show_error_dialog(const char* text, uint16_t next_view);
extern bool bluetooth_recording_state(BtManager::State state);
extern bool connect_to_bluetooth_host(const bt_host_item_t* host, const char* source);
extern void request_bluetooth_disconnect();
extern void show_wifi_connection_failed(const char* text);
extern void show_fatal_error_f(bool fatal, const char* format, ...);

#ifdef BUILD_CLOUD_FEATURES
extern CloudUpload g_cloud_upload;
extern volatile bool g_pending_cloud_upload_complete;
extern CloudUpload::Status g_pending_cloud_upload_status;
extern bool show_cloud_upload_view(CloudUpload* uploader, const char* targetName);
#endif

void on_bluetooth_state_changed(BtManager::State state)
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

void on_bluetooth_paired(const BtManager::PairedDevice& device)
{
    g_pending_paired_device = device;
    g_pending_bluetooth_pairing = true;
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

    // Fade quickly as feedback because the analysis calls can take a moment.
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
    #ifdef BUILD_CLOUD_FEATURES
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
    #else
    if (disk_ok && rec_ok)
    {
        snprintf(text,
                 sizeof(text),
                 "Disk: %s/%s free (%u%%)\nFiles: %lu total\nLatest: %s",
                 free_text,
                 total_text,
                 static_cast<unsigned>(DiskStats::freeDiskSpacePercent()),
                 static_cast<unsigned long>(DiskStats::totalRecFilesStored()),
                 latest_file);
    }
    else
    {
        snprintf(text,
                 sizeof(text),
                 "System Info\nDisk: unavailable\nREC scan: unavailable\nLatest: %s",
                 latest_file);
    }
    #endif

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
    if (g_wifi_connect_waiting)
    {
        g_wifi_connect_waiting = false;
        if (wifi_manager)
        {
            wifi_manager->disconnect();
        }
        if (gui)
        {
            const uint16_t return_view = conn_waiting_return_view_id();
            if (!gui->showView(return_view))
            {
                gui->showView(FLYGUI_VIEW_SCROLL);
            }
        }
        return;
    }

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

void onclick_scroll_exit(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "scroll view exit selected");
    ScrollView* scroll_view = get_scroll_view();
    if (scroll_view && scroll_view->isCloudContext())
    {
        ESP_LOGI(MAINTAG, "cloud scroll exit selected; rebooting");
        delay(50);
        esp_restart();
        return;
    }
    if (scroll_view && scroll_view->isWifiContext() && wifi_manager && wifi_manager->wifiHasStarted())
    {
        ESP_LOGI(MAINTAG, "Wi-Fi scroll exit selected after Wi-Fi start; rebooting");
        delay(50);
        esp_restart();
        return;
    }

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

void onclick_wifi_station(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "scroll wifi station selected: index=%ld", static_cast<long>(value));
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
        ESP_LOGW(MAINTAG, "could not start Wi-Fi station connection: %s", wifi_manager->statusName());
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

void onclick_wifi_ap(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "scroll wifi ap selected: index=%ld", static_cast<long>(value));
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
void onclick_cloud_upload(int32_t value, uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "scroll cloud upload selected: index=%ld", static_cast<long>(value));

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
        show_error_dialog(status.message[0] != '\0' ? status.message : "Cloud upload failed to start", FLYGUI_VIEW_SCROLL);
        return;
    }

    if (!show_cloud_upload_view(&g_cloud_upload, endpoint->url))
    {
        g_cloud_upload.cancel();
        show_fatal_error_f(false, "Cloud upload view failed");
    }
}

void cloud_upload_cancel(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    ESP_LOGI(MAINTAG, "cloud upload cancel selected");
    g_cloud_upload.cancel();
}
#endif

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

    if (wifi_manager && wifi_manager->status() == WifiManager::Status::StationConnected)
    {
        if (!show_wifi_sta_mode_view(true))
        {
            show_fatal_error_f(false, "Wi-Fi station view failed");
        }
        return;
    }

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

void on_pairing_success_dialog_dismissed()
{
    ESP_LOGI(MAINTAG, "pairing confirmation dismissed; Bluetooth auto-record can resume");
    g_suppress_bluetooth_auto_recording = false;
    if (bluetooth_recording_state(BtManager::state()))
    {
        ESP_LOGI(MAINTAG, "Bluetooth was connected while pairing confirmation was shown; queueing recording start");
        g_pending_bluetooth_recording = true;
    }
}

#ifdef BUILD_CLOUD_FEATURES
void on_cloud_upload_complete(const CloudUpload::Status& status)
{
    g_pending_cloud_upload_status = status;
    g_pending_cloud_upload_complete = true;
}

void on_cloud_upload_dialog_dismissed()
{
    ESP_LOGI(MAINTAG, "cloud upload confirmation dismissed; rebooting");
    delay(50);
    esp_restart();
}
#endif

void on_wifi_scan_finished(const wifi_item_t* item)
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
    update_conn_waiting_wifi_target(item->ssid);
    if (!wifi_manager->connectToHotspot(item))
    {
        ESP_LOGW(MAINTAG, "could not connect to scanned Wi-Fi station: %s", wifi_manager->statusName());
        show_wifi_connection_failed("Wi-Fi connection failed to start");
    }
}
