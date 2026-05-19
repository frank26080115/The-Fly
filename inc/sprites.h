#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(ARDUINO)
#include <pgmspace.h>
#endif

#ifndef PROGMEM
#define PROGMEM
#endif

#define SPRIT_BATT_FULL_WIDTH 20u
#define SPRIT_BATT_FULL_HEIGHT 10u
#define SPRIT_BATT_FULL_BYTES 121u
extern const uint8_t sprit_batt_full[];

#define SPRIT_BATT_FULL_CHARGING_WIDTH 20u
#define SPRIT_BATT_FULL_CHARGING_HEIGHT 10u
#define SPRIT_BATT_FULL_CHARGING_BYTES 138u
extern const uint8_t sprit_batt_full_charging[];

#define SPRIT_BATT_LOW_WIDTH 20u
#define SPRIT_BATT_LOW_HEIGHT 10u
#define SPRIT_BATT_LOW_BYTES 161u
extern const uint8_t sprit_batt_low[];

#define SPRIT_BATT_LOW_CHARGING_WIDTH 20u
#define SPRIT_BATT_LOW_CHARGING_HEIGHT 10u
#define SPRIT_BATT_LOW_CHARGING_BYTES 172u
extern const uint8_t sprit_batt_low_charging[];

#define SPRIT_BATT_MEDIUM_WIDTH 20u
#define SPRIT_BATT_MEDIUM_HEIGHT 10u
#define SPRIT_BATT_MEDIUM_BYTES 184u
extern const uint8_t sprit_batt_medium[];

#define SPRIT_BATT_MEDIUM_CHARGING_WIDTH 20u
#define SPRIT_BATT_MEDIUM_CHARGING_HEIGHT 10u
#define SPRIT_BATT_MEDIUM_CHARGING_BYTES 178u
extern const uint8_t sprit_batt_medium_charging[];

#define SPRIT_BLUETOOTH_180X150_WIDTH 180u
#define SPRIT_BLUETOOTH_180X150_HEIGHT 150u
#define SPRIT_BLUETOOTH_180X150_BYTES 37097u
extern const uint8_t sprit_bluetooth_180x150[];

#define SPRIT_BTN_BLUETOOTH_WIDTH 106u
#define SPRIT_BTN_BLUETOOTH_HEIGHT 114u
#define SPRIT_BTN_BLUETOOTH_BYTES 11716u
extern const uint8_t sprit_btn_bluetooth[];

#define SPRIT_BTN_LAPTOP_WIDTH 106u
#define SPRIT_BTN_LAPTOP_HEIGHT 114u
#define SPRIT_BTN_LAPTOP_BYTES 9524u
extern const uint8_t sprit_btn_laptop[];

#define SPRIT_BTN_MEMO_WIDTH 106u
#define SPRIT_BTN_MEMO_HEIGHT 114u
#define SPRIT_BTN_MEMO_BYTES 15575u
extern const uint8_t sprit_btn_memo[];

#define SPRIT_BTN_QUESTIONMARK_WIDTH 106u
#define SPRIT_BTN_QUESTIONMARK_HEIGHT 114u
#define SPRIT_BTN_QUESTIONMARK_BYTES 2320u
extern const uint8_t sprit_btn_questionmark[];

#define SPRIT_BTN_SMARTPHONE_WIDTH 106u
#define SPRIT_BTN_SMARTPHONE_HEIGHT 114u
#define SPRIT_BTN_SMARTPHONE_BYTES 13008u
extern const uint8_t sprit_btn_smartphone[];

#define SPRIT_BTN_WIFI_WIDTH 106u
#define SPRIT_BTN_WIFI_HEIGHT 114u
#define SPRIT_BTN_WIFI_BYTES 9170u
extern const uint8_t sprit_btn_wifi[];

#define SPRIT_BTN_WIFIHOME_WIDTH 106u
#define SPRIT_BTN_WIFIHOME_HEIGHT 114u
#define SPRIT_BTN_WIFIHOME_BYTES 12786u
extern const uint8_t sprit_btn_wifihome[];

#define SPRIT_BTN_WIFISEARCH_WIDTH 106u
#define SPRIT_BTN_WIFISEARCH_HEIGHT 114u
#define SPRIT_BTN_WIFISEARCH_BYTES 10069u
extern const uint8_t sprit_btn_wifisearch[];

#define SPRIT_CANCEL_60_WIDTH 60u
#define SPRIT_CANCEL_60_HEIGHT 60u
#define SPRIT_CANCEL_60_BYTES 6344u
extern const uint8_t sprit_cancel_60[];

#define SPRIT_CLOUD_180X150_WIDTH 180u
#define SPRIT_CLOUD_180X150_HEIGHT 150u
#define SPRIT_CLOUD_180X150_BYTES 25169u
extern const uint8_t sprit_cloud_180x150[];

#define SPRIT_ERROR_LARGE_WIDTH 116u
#define SPRIT_ERROR_LARGE_HEIGHT 111u
#define SPRIT_ERROR_LARGE_BYTES 22586u
extern const uint8_t sprit_error_large[];

#define SPRIT_EXIT_50_WIDTH 50u
#define SPRIT_EXIT_50_HEIGHT 50u
#define SPRIT_EXIT_50_BYTES 4606u
extern const uint8_t sprit_exit_50[];

#define SPRIT_HANDSHAKE_60_WIDTH 60u
#define SPRIT_HANDSHAKE_60_HEIGHT 60u
#define SPRIT_HANDSHAKE_60_BYTES 3807u
extern const uint8_t sprit_handshake_60[];

#define SPRIT_HOURGLASS_60_1_WIDTH 60u
#define SPRIT_HOURGLASS_60_1_HEIGHT 60u
#define SPRIT_HOURGLASS_60_1_BYTES 4268u
extern const uint8_t sprit_hourglass_60_1[];

#define SPRIT_HOURGLASS_60_2_WIDTH 60u
#define SPRIT_HOURGLASS_60_2_HEIGHT 60u
#define SPRIT_HOURGLASS_60_2_BYTES 4200u
extern const uint8_t sprit_hourglass_60_2[];

#define SPRIT_HOURGLASS_60_3_WIDTH 60u
#define SPRIT_HOURGLASS_60_3_HEIGHT 60u
#define SPRIT_HOURGLASS_60_3_BYTES 4246u
extern const uint8_t sprit_hourglass_60_3[];

#define SPRIT_NTP_180X150_WIDTH 180u
#define SPRIT_NTP_180X150_HEIGHT 150u
#define SPRIT_NTP_180X150_BYTES 29779u
extern const uint8_t sprit_ntp_180x150[];

#define SPRIT_SPLASH_WIDTH 320u
#define SPRIT_SPLASH_HEIGHT 240u
#define SPRIT_SPLASH_BYTES 34033u
extern const uint8_t sprit_splash[];

#define SPRIT_WIFI_180X150_WIDTH 180u
#define SPRIT_WIFI_180X150_HEIGHT 150u
#define SPRIT_WIFI_180X150_BYTES 28182u
extern const uint8_t sprit_wifi_180x150[];
