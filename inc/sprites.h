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

#define SPRIT_BTN_BLUETOOTH_WIDTH 106u
#define SPRIT_BTN_BLUETOOTH_HEIGHT 114u
#define SPRIT_BTN_BLUETOOTH_BYTES 11686u
extern const uint8_t sprit_btn_bluetooth[];

#define SPRIT_BTN_LAPTOP_WIDTH 106u
#define SPRIT_BTN_LAPTOP_HEIGHT 114u
#define SPRIT_BTN_LAPTOP_BYTES 9512u
extern const uint8_t sprit_btn_laptop[];

#define SPRIT_BTN_MEMO_WIDTH 106u
#define SPRIT_BTN_MEMO_HEIGHT 114u
#define SPRIT_BTN_MEMO_BYTES 15538u
extern const uint8_t sprit_btn_memo[];

#define SPRIT_BTN_SMARTPHONE_WIDTH 106u
#define SPRIT_BTN_SMARTPHONE_HEIGHT 114u
#define SPRIT_BTN_SMARTPHONE_BYTES 13009u
extern const uint8_t sprit_btn_smartphone[];

#define SPRIT_BTN_WIFI_WIDTH 106u
#define SPRIT_BTN_WIFI_HEIGHT 114u
#define SPRIT_BTN_WIFI_BYTES 9133u
extern const uint8_t sprit_btn_wifi[];

#define SPRIT_BTN_WIFIHOME_WIDTH 106u
#define SPRIT_BTN_WIFIHOME_HEIGHT 114u
#define SPRIT_BTN_WIFIHOME_BYTES 12754u
extern const uint8_t sprit_btn_wifihome[];

#define SPRIT_ERROR_LARGE_WIDTH 116u
#define SPRIT_ERROR_LARGE_HEIGHT 111u
#define SPRIT_ERROR_LARGE_BYTES 22584u
extern const uint8_t sprit_error_large[];

#define SPRIT_SPLASH_WIDTH 320u
#define SPRIT_SPLASH_HEIGHT 240u
#define SPRIT_SPLASH_BYTES 33837u
extern const uint8_t sprit_splash[];
