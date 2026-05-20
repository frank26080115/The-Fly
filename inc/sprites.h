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

#define SPRIT_BLUETOOTH_50_WIDTH 50u
#define SPRIT_BLUETOOTH_50_HEIGHT 50u
#define SPRIT_BLUETOOTH_50_BYTES 3355u
extern const uint8_t sprit_bluetooth_50[];

#define SPRIT_BTN_BLUETOOTH_WIDTH 106u
#define SPRIT_BTN_BLUETOOTH_HEIGHT 114u
#define SPRIT_BTN_BLUETOOTH_BYTES 11716u
extern const uint8_t sprit_btn_bluetooth[];

#define SPRIT_BTN_BLUETOOTH_PAIRING_WIDTH 106u
#define SPRIT_BTN_BLUETOOTH_PAIRING_HEIGHT 114u
#define SPRIT_BTN_BLUETOOTH_PAIRING_BYTES 15463u
extern const uint8_t sprit_btn_bluetooth_pairing[];

#define SPRIT_BTN_CLOUD_WIDTH 106u
#define SPRIT_BTN_CLOUD_HEIGHT 114u
#define SPRIT_BTN_CLOUD_BYTES 10280u
extern const uint8_t sprit_btn_cloud[];

#define SPRIT_BTN_END_WIDTH 100u
#define SPRIT_BTN_END_HEIGHT 100u
#define SPRIT_BTN_END_BYTES 14006u
extern const uint8_t sprit_btn_end[];

#define SPRIT_BTN_INFO_WIDTH 106u
#define SPRIT_BTN_INFO_HEIGHT 114u
#define SPRIT_BTN_INFO_BYTES 12630u
extern const uint8_t sprit_btn_info[];

#define SPRIT_BTN_LAPTOP_WIDTH 106u
#define SPRIT_BTN_LAPTOP_HEIGHT 114u
#define SPRIT_BTN_LAPTOP_BYTES 9524u
extern const uint8_t sprit_btn_laptop[];

#define SPRIT_BTN_MEMO_WIDTH 106u
#define SPRIT_BTN_MEMO_HEIGHT 114u
#define SPRIT_BTN_MEMO_BYTES 15575u
extern const uint8_t sprit_btn_memo[];

#define SPRIT_BTN_MIC_WIDTH 100u
#define SPRIT_BTN_MIC_HEIGHT 100u
#define SPRIT_BTN_MIC_BYTES 10574u
extern const uint8_t sprit_btn_mic[];

#define SPRIT_BTN_NTP_WIDTH 106u
#define SPRIT_BTN_NTP_HEIGHT 114u
#define SPRIT_BTN_NTP_BYTES 11267u
extern const uint8_t sprit_btn_ntp[];

#define SPRIT_BTN_QUESTIONMARK_WIDTH 106u
#define SPRIT_BTN_QUESTIONMARK_HEIGHT 114u
#define SPRIT_BTN_QUESTIONMARK_BYTES 2320u
extern const uint8_t sprit_btn_questionmark[];

#define SPRIT_BTN_SMARTPHONE_WIDTH 106u
#define SPRIT_BTN_SMARTPHONE_HEIGHT 114u
#define SPRIT_BTN_SMARTPHONE_BYTES 13008u
extern const uint8_t sprit_btn_smartphone[];

#define SPRIT_BTN_SPEAKER_WIDTH 100u
#define SPRIT_BTN_SPEAKER_HEIGHT 100u
#define SPRIT_BTN_SPEAKER_BYTES 9678u
extern const uint8_t sprit_btn_speaker[];

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

#define SPRIT_CHECKMARK_100_WIDTH 100u
#define SPRIT_CHECKMARK_100_HEIGHT 100u
#define SPRIT_CHECKMARK_100_BYTES 6609u
extern const uint8_t sprit_checkmark_100[];

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

#define SPRIT_INFO_100_WIDTH 100u
#define SPRIT_INFO_100_HEIGHT 100u
#define SPRIT_INFO_100_BYTES 12541u
extern const uint8_t sprit_info_100[];

#define SPRIT_MEMOTYPE_IDEA_WIDTH 50u
#define SPRIT_MEMOTYPE_IDEA_HEIGHT 50u
#define SPRIT_MEMOTYPE_IDEA_BYTES 3821u
extern const uint8_t sprit_memotype_idea[];

#define SPRIT_MEMOTYPE_JOURNAL_WIDTH 50u
#define SPRIT_MEMOTYPE_JOURNAL_HEIGHT 50u
#define SPRIT_MEMOTYPE_JOURNAL_BYTES 3514u
extern const uint8_t sprit_memotype_journal[];

#define SPRIT_MEMOTYPE_NOTE_WIDTH 50u
#define SPRIT_MEMOTYPE_NOTE_HEIGHT 50u
#define SPRIT_MEMOTYPE_NOTE_BYTES 3982u
extern const uint8_t sprit_memotype_note[];

#define SPRIT_MEMOTYPE_REMINDER_WIDTH 50u
#define SPRIT_MEMOTYPE_REMINDER_HEIGHT 50u
#define SPRIT_MEMOTYPE_REMINDER_BYTES 4524u
extern const uint8_t sprit_memotype_reminder[];

#define SPRIT_MEMOTYPE_TODO_WIDTH 50u
#define SPRIT_MEMOTYPE_TODO_HEIGHT 50u
#define SPRIT_MEMOTYPE_TODO_BYTES 3814u
extern const uint8_t sprit_memotype_todo[];

#define SPRIT_MUTED_X_WIDTH 40u
#define SPRIT_MUTED_X_HEIGHT 40u
#define SPRIT_MUTED_X_BYTES 2316u
extern const uint8_t sprit_muted_x[];

#define SPRIT_NTP_180X150_WIDTH 180u
#define SPRIT_NTP_180X150_HEIGHT 150u
#define SPRIT_NTP_180X150_BYTES 29779u
extern const uint8_t sprit_ntp_180x150[];

#define SPRIT_PICKUP_WIDTH 50u
#define SPRIT_PICKUP_HEIGHT 50u
#define SPRIT_PICKUP_BYTES 2569u
extern const uint8_t sprit_pickup[];

#define SPRIT_PICKUPDOWN_WIDTH 50u
#define SPRIT_PICKUPDOWN_HEIGHT 50u
#define SPRIT_PICKUPDOWN_BYTES 2741u
extern const uint8_t sprit_pickupdown[];

#define SPRIT_SPLASH_WIDTH 320u
#define SPRIT_SPLASH_HEIGHT 240u
#define SPRIT_SPLASH_BYTES 34033u
extern const uint8_t sprit_splash[];

#define SPRIT_THUMBSUP_100_WIDTH 100u
#define SPRIT_THUMBSUP_100_HEIGHT 100u
#define SPRIT_THUMBSUP_100_BYTES 11690u
extern const uint8_t sprit_thumbsup_100[];

#define SPRIT_WIFI_180X150_WIDTH 180u
#define SPRIT_WIFI_180X150_HEIGHT 150u
#define SPRIT_WIFI_180X150_BYTES 28182u
extern const uint8_t sprit_wifi_180x150[];
