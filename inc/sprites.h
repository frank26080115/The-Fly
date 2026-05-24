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

#define SPRIT_BLUETOOTH_100_WIDTH 100u
#define SPRIT_BLUETOOTH_100_HEIGHT 100u
#define SPRIT_BLUETOOTH_100_BYTES 12580u
extern const uint8_t sprit_bluetooth_100[];

#define SPRIT_BLUETOOTH_50_WIDTH 50u
#define SPRIT_BLUETOOTH_50_HEIGHT 50u
#define SPRIT_BLUETOOTH_50_BYTES 3819u
extern const uint8_t sprit_bluetooth_50[];

#define SPRIT_BLUETOOTH_X_50_WIDTH 50u
#define SPRIT_BLUETOOTH_X_50_HEIGHT 50u
#define SPRIT_BLUETOOTH_X_50_BYTES 4027u
extern const uint8_t sprit_bluetooth_x_50[];

#define SPRIT_BTPAIRING_100_WIDTH 100u
#define SPRIT_BTPAIRING_100_HEIGHT 100u
#define SPRIT_BTPAIRING_100_BYTES 12906u
extern const uint8_t sprit_btpairing_100[];

#define SPRIT_CANCEL_60_WIDTH 60u
#define SPRIT_CANCEL_60_HEIGHT 60u
#define SPRIT_CANCEL_60_BYTES 6323u
extern const uint8_t sprit_cancel_60[];

#define SPRIT_CANCELDOOR_50_WIDTH 50u
#define SPRIT_CANCELDOOR_50_HEIGHT 50u
#define SPRIT_CANCELDOOR_50_BYTES 4390u
extern const uint8_t sprit_canceldoor_50[];

#define SPRIT_CLOUDUPLOAD_100_WIDTH 100u
#define SPRIT_CLOUDUPLOAD_100_HEIGHT 100u
#define SPRIT_CLOUDUPLOAD_100_BYTES 11111u
extern const uint8_t sprit_cloudupload_100[];

#define SPRIT_GENERICDEVICE_BT_100_WIDTH 100u
#define SPRIT_GENERICDEVICE_BT_100_HEIGHT 100u
#define SPRIT_GENERICDEVICE_BT_100_BYTES 14592u
extern const uint8_t sprit_genericdevice_bt_100[];

#define SPRIT_GENERICWIFIROUTER_100_WIDTH 100u
#define SPRIT_GENERICWIFIROUTER_100_HEIGHT 100u
#define SPRIT_GENERICWIFIROUTER_100_BYTES 13166u
extern const uint8_t sprit_genericwifirouter_100[];

#define SPRIT_GREENCHECKMARK_100_WIDTH 100u
#define SPRIT_GREENCHECKMARK_100_HEIGHT 100u
#define SPRIT_GREENCHECKMARK_100_BYTES 8216u
extern const uint8_t sprit_greencheckmark_100[];

#define SPRIT_HOME_100_WIDTH 100u
#define SPRIT_HOME_100_HEIGHT 100u
#define SPRIT_HOME_100_BYTES 13241u
extern const uint8_t sprit_home_100[];

#define SPRIT_HOME_WIFI_100_WIDTH 100u
#define SPRIT_HOME_WIFI_100_HEIGHT 100u
#define SPRIT_HOME_WIFI_100_BYTES 14471u
extern const uint8_t sprit_home_wifi_100[];

#define SPRIT_HOURGLASS_30_OVERLAY_WIDTH 30u
#define SPRIT_HOURGLASS_30_OVERLAY_HEIGHT 30u
#define SPRIT_HOURGLASS_30_OVERLAY_BYTES 2259u
extern const uint8_t sprit_hourglass_30_overlay[];

#define SPRIT_HOURGLASS_60_1_WIDTH 60u
#define SPRIT_HOURGLASS_60_1_HEIGHT 60u
#define SPRIT_HOURGLASS_60_1_BYTES 4260u
extern const uint8_t sprit_hourglass_60_1[];

#define SPRIT_HOURGLASS_60_2_WIDTH 60u
#define SPRIT_HOURGLASS_60_2_HEIGHT 60u
#define SPRIT_HOURGLASS_60_2_BYTES 4189u
extern const uint8_t sprit_hourglass_60_2[];

#define SPRIT_HOURGLASS_60_3_WIDTH 60u
#define SPRIT_HOURGLASS_60_3_HEIGHT 60u
#define SPRIT_HOURGLASS_60_3_BYTES 4236u
extern const uint8_t sprit_hourglass_60_3[];

#define SPRIT_INFO_100_WIDTH 100u
#define SPRIT_INFO_100_HEIGHT 100u
#define SPRIT_INFO_100_BYTES 16257u
extern const uint8_t sprit_info_100[];

#define SPRIT_LAPTOP_100_WIDTH 100u
#define SPRIT_LAPTOP_100_HEIGHT 100u
#define SPRIT_LAPTOP_100_BYTES 9336u
extern const uint8_t sprit_laptop_100[];

#define SPRIT_LAPTOP_BT_100_WIDTH 100u
#define SPRIT_LAPTOP_BT_100_HEIGHT 100u
#define SPRIT_LAPTOP_BT_100_BYTES 10462u
extern const uint8_t sprit_laptop_bt_100[];

#define SPRIT_LOWBATT_100_WIDTH 100u
#define SPRIT_LOWBATT_100_HEIGHT 100u
#define SPRIT_LOWBATT_100_BYTES 8044u
extern const uint8_t sprit_lowbatt_100[];

#define SPRIT_MEMO_100_WIDTH 100u
#define SPRIT_MEMO_100_HEIGHT 100u
#define SPRIT_MEMO_100_BYTES 14449u
extern const uint8_t sprit_memo_100[];

#define SPRIT_MEMOTYPE_IDEA_WIDTH 50u
#define SPRIT_MEMOTYPE_IDEA_HEIGHT 50u
#define SPRIT_MEMOTYPE_IDEA_BYTES 3809u
extern const uint8_t sprit_memotype_idea[];

#define SPRIT_MEMOTYPE_JOURNAL_WIDTH 50u
#define SPRIT_MEMOTYPE_JOURNAL_HEIGHT 50u
#define SPRIT_MEMOTYPE_JOURNAL_BYTES 3513u
extern const uint8_t sprit_memotype_journal[];

#define SPRIT_MEMOTYPE_NOTE_WIDTH 50u
#define SPRIT_MEMOTYPE_NOTE_HEIGHT 50u
#define SPRIT_MEMOTYPE_NOTE_BYTES 3981u
extern const uint8_t sprit_memotype_note[];

#define SPRIT_MEMOTYPE_REMINDER_WIDTH 50u
#define SPRIT_MEMOTYPE_REMINDER_HEIGHT 50u
#define SPRIT_MEMOTYPE_REMINDER_BYTES 4521u
extern const uint8_t sprit_memotype_reminder[];

#define SPRIT_MEMOTYPE_TODO_WIDTH 50u
#define SPRIT_MEMOTYPE_TODO_HEIGHT 50u
#define SPRIT_MEMOTYPE_TODO_BYTES 3811u
extern const uint8_t sprit_memotype_todo[];

#define SPRIT_MIC_100_WIDTH 100u
#define SPRIT_MIC_100_HEIGHT 100u
#define SPRIT_MIC_100_BYTES 10530u
extern const uint8_t sprit_mic_100[];

#define SPRIT_MUTED_X_WIDTH 40u
#define SPRIT_MUTED_X_HEIGHT 40u
#define SPRIT_MUTED_X_BYTES 2315u
extern const uint8_t sprit_muted_x[];

#define SPRIT_NTPSYNC_100_WIDTH 100u
#define SPRIT_NTPSYNC_100_HEIGHT 100u
#define SPRIT_NTPSYNC_100_BYTES 11983u
extern const uint8_t sprit_ntpsync_100[];

#define SPRIT_OVERLAY_BIRD_50_WIDTH 50u
#define SPRIT_OVERLAY_BIRD_50_HEIGHT 50u
#define SPRIT_OVERLAY_BIRD_50_BYTES 4143u
extern const uint8_t sprit_overlay_bird_50[];

#define SPRIT_OVERLAY_BT_50_WIDTH 50u
#define SPRIT_OVERLAY_BT_50_HEIGHT 50u
#define SPRIT_OVERLAY_BT_50_BYTES 4126u
extern const uint8_t sprit_overlay_bt_50[];

#define SPRIT_OVERLAY_CAT_50_WIDTH 50u
#define SPRIT_OVERLAY_CAT_50_HEIGHT 50u
#define SPRIT_OVERLAY_CAT_50_BYTES 5974u
extern const uint8_t sprit_overlay_cat_50[];

#define SPRIT_OVERLAY_CIRCLE_40_WIDTH 40u
#define SPRIT_OVERLAY_CIRCLE_40_HEIGHT 40u
#define SPRIT_OVERLAY_CIRCLE_40_BYTES 2847u
extern const uint8_t sprit_overlay_circle_40[];

#define SPRIT_OVERLAY_CLOUD_50_WIDTH 50u
#define SPRIT_OVERLAY_CLOUD_50_HEIGHT 50u
#define SPRIT_OVERLAY_CLOUD_50_BYTES 3299u
extern const uint8_t sprit_overlay_cloud_50[];

#define SPRIT_OVERLAY_DOG_50_WIDTH 50u
#define SPRIT_OVERLAY_DOG_50_HEIGHT 50u
#define SPRIT_OVERLAY_DOG_50_BYTES 4944u
extern const uint8_t sprit_overlay_dog_50[];

#define SPRIT_OVERLAY_HOME_50_WIDTH 50u
#define SPRIT_OVERLAY_HOME_50_HEIGHT 50u
#define SPRIT_OVERLAY_HOME_50_BYTES 4166u
extern const uint8_t sprit_overlay_home_50[];

#define SPRIT_OVERLAY_SQUARE_40_WIDTH 40u
#define SPRIT_OVERLAY_SQUARE_40_HEIGHT 40u
#define SPRIT_OVERLAY_SQUARE_40_BYTES 2262u
extern const uint8_t sprit_overlay_square_40[];

#define SPRIT_OVERLAY_TRIANGLE_40_WIDTH 40u
#define SPRIT_OVERLAY_TRIANGLE_40_HEIGHT 40u
#define SPRIT_OVERLAY_TRIANGLE_40_BYTES 2097u
extern const uint8_t sprit_overlay_triangle_40[];

#define SPRIT_OVERLAY_WIFI_50_WIDTH 50u
#define SPRIT_OVERLAY_WIFI_50_HEIGHT 50u
#define SPRIT_OVERLAY_WIFI_50_BYTES 3385u
extern const uint8_t sprit_overlay_wifi_50[];

#define SPRIT_PICKUP_WIDTH 50u
#define SPRIT_PICKUP_HEIGHT 50u
#define SPRIT_PICKUP_BYTES 2550u
extern const uint8_t sprit_pickup[];

#define SPRIT_PICKUPDOWN_WIDTH 50u
#define SPRIT_PICKUPDOWN_HEIGHT 50u
#define SPRIT_PICKUPDOWN_BYTES 2727u
extern const uint8_t sprit_pickupdown[];

#define SPRIT_SLEEP_100_WIDTH 100u
#define SPRIT_SLEEP_100_HEIGHT 100u
#define SPRIT_SLEEP_100_BYTES 12460u
extern const uint8_t sprit_sleep_100[];

#define SPRIT_SMARTPHONE_100_WIDTH 100u
#define SPRIT_SMARTPHONE_100_HEIGHT 100u
#define SPRIT_SMARTPHONE_100_BYTES 10922u
extern const uint8_t sprit_smartphone_100[];

#define SPRIT_SMARTPHONE_AP_100_WIDTH 100u
#define SPRIT_SMARTPHONE_AP_100_HEIGHT 100u
#define SPRIT_SMARTPHONE_AP_100_BYTES 12364u
extern const uint8_t sprit_smartphone_ap_100[];

#define SPRIT_SMARTPHONE_BT_100_WIDTH 100u
#define SPRIT_SMARTPHONE_BT_100_HEIGHT 100u
#define SPRIT_SMARTPHONE_BT_100_BYTES 12393u
extern const uint8_t sprit_smartphone_bt_100[];

#define SPRIT_SPEAKER_100_WIDTH 100u
#define SPRIT_SPEAKER_100_HEIGHT 100u
#define SPRIT_SPEAKER_100_BYTES 11664u
extern const uint8_t sprit_speaker_100[];

#define SPRIT_SPLASH_WIDTH 320u
#define SPRIT_SPLASH_HEIGHT 240u
#define SPRIT_SPLASH_BYTES 33837u
extern const uint8_t sprit_splash[];

#define SPRIT_TABLET_100_WIDTH 100u
#define SPRIT_TABLET_100_HEIGHT 100u
#define SPRIT_TABLET_100_BYTES 10864u
extern const uint8_t sprit_tablet_100[];

#define SPRIT_TABLET_BT_100_WIDTH 100u
#define SPRIT_TABLET_BT_100_HEIGHT 100u
#define SPRIT_TABLET_BT_100_BYTES 12365u
extern const uint8_t sprit_tablet_bt_100[];

#define SPRIT_THUMBSUP_100_WIDTH 100u
#define SPRIT_THUMBSUP_100_HEIGHT 100u
#define SPRIT_THUMBSUP_100_BYTES 13906u
extern const uint8_t sprit_thumbsup_100[];

#define SPRIT_TRASH_100_WIDTH 100u
#define SPRIT_TRASH_100_HEIGHT 100u
#define SPRIT_TRASH_100_BYTES 12067u
extern const uint8_t sprit_trash_100[];

#define SPRIT_TRASH_50_WIDTH 50u
#define SPRIT_TRASH_50_HEIGHT 50u
#define SPRIT_TRASH_50_BYTES 3604u
extern const uint8_t sprit_trash_50[];

#define SPRIT_UNKNOWN_BT_100_WIDTH 100u
#define SPRIT_UNKNOWN_BT_100_HEIGHT 100u
#define SPRIT_UNKNOWN_BT_100_BYTES 15034u
extern const uint8_t sprit_unknown_bt_100[];

#define SPRIT_WARNING_100_WIDTH 100u
#define SPRIT_WARNING_100_HEIGHT 100u
#define SPRIT_WARNING_100_BYTES 12579u
extern const uint8_t sprit_warning_100[];

#define SPRIT_WIFI_100_WIDTH 100u
#define SPRIT_WIFI_100_HEIGHT 100u
#define SPRIT_WIFI_100_BYTES 10195u
extern const uint8_t sprit_wifi_100[];

#define SPRIT_WIFISEARCH_100_WIDTH 100u
#define SPRIT_WIFISEARCH_100_HEIGHT 100u
#define SPRIT_WIFISEARCH_100_BYTES 12084u
extern const uint8_t sprit_wifisearch_100[];

#define SPRIT_XCIRCLE_100_WIDTH 100u
#define SPRIT_XCIRCLE_100_HEIGHT 100u
#define SPRIT_XCIRCLE_100_BYTES 16739u
extern const uint8_t sprit_xcircle_100[];
