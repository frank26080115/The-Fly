#pragma once

#include "thefly_common.h"

#include <stdint.h>

#include "BluetoothManager.h"
#include "CloudUpload.h"
#include "NtpSync.h"
#include "WifiManager.h"

void onclick_main_bluetooth(uint32_t pressDurationMs);
void onclick_main_info(uint32_t pressDurationMs);
void onclick_main_files(uint32_t pressDurationMs);
void onclick_main_wifi(uint32_t pressDurationMs);
void onclick_main_memo(uint32_t pressDurationMs);
void onclick_main_smartphone(uint32_t pressDurationMs);
void onclick_main_laptop(uint32_t pressDurationMs);

void onclick_scroll_exit(uint32_t pressDurationMs);
void onclick_bluetooth_host(int32_t value, uint32_t pressDurationMs);
void onclick_bluetooth_pair(int32_t value, uint32_t pressDurationMs);
void onclick_wifi_scan_and_connect(int32_t value, uint32_t pressDurationMs);
void onclick_wifi_station(int32_t value, uint32_t pressDurationMs);
void onclick_wifi_ap(int32_t value, uint32_t pressDurationMs);
void onclick_ntp_sync(int32_t value, uint32_t pressDurationMs);
void onclick_bt_show_info(int32_t value, uint32_t pressDurationMs);
void onclick_wifi_show_info(int32_t value, uint32_t pressDurationMs);
void onclick_file_wav(int32_t value, uint32_t pressDurationMs);
void onclick_file_show_info(int32_t value, uint32_t pressDurationMs);

void conn_waiting_cancel(uint32_t pressDurationMs);

void on_bluetooth_state_changed(BtManager::State state);
void on_bluetooth_paired(const BtManager::PairedDevice& device);
void on_wifi_scan_finished(const wifi_item_t* item);
void on_ntp_sync_complete(const NtpSync::Result& result);
void on_pairing_success_dialog_dismissed();

#ifdef BUILD_CLOUD_FEATURES
void onclick_cloud_upload(int32_t value, uint32_t pressDurationMs);
void cloud_upload_cancel(uint32_t pressDurationMs);
void on_cloud_upload_complete(const CloudUpload::Status& status);
void on_cloud_upload_dialog_dismissed();
#endif
