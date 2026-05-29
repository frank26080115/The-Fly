#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(ARDUINO)
#include <pgmspace.h>
#endif

#ifndef PROGMEM
#define PROGMEM
#endif

#define SPRITE_BATT_FULL_WIDTH 20u
#define SPRITE_BATT_FULL_HEIGHT 10u
#define SPRITE_BATT_FULL_BYTES 121u
extern const uint8_t sprite_batt_full[];

#define SPRITE_BATT_FULL_CHARGING_WIDTH 20u
#define SPRITE_BATT_FULL_CHARGING_HEIGHT 10u
#define SPRITE_BATT_FULL_CHARGING_BYTES 138u
extern const uint8_t sprite_batt_full_charging[];

#define SPRITE_BATT_LOW_WIDTH 20u
#define SPRITE_BATT_LOW_HEIGHT 10u
#define SPRITE_BATT_LOW_BYTES 161u
extern const uint8_t sprite_batt_low[];

#define SPRITE_BATT_LOW_CHARGING_WIDTH 20u
#define SPRITE_BATT_LOW_CHARGING_HEIGHT 10u
#define SPRITE_BATT_LOW_CHARGING_BYTES 172u
extern const uint8_t sprite_batt_low_charging[];

#define SPRITE_BATT_MEDIUM_WIDTH 20u
#define SPRITE_BATT_MEDIUM_HEIGHT 10u
#define SPRITE_BATT_MEDIUM_BYTES 184u
extern const uint8_t sprite_batt_medium[];

#define SPRITE_BATT_MEDIUM_CHARGING_WIDTH 20u
#define SPRITE_BATT_MEDIUM_CHARGING_HEIGHT 10u
#define SPRITE_BATT_MEDIUM_CHARGING_BYTES 178u
extern const uint8_t sprite_batt_medium_charging[];

#define SPRITE_BLUETOOTH_100_WIDTH 100u
#define SPRITE_BLUETOOTH_100_HEIGHT 100u
#define SPRITE_BLUETOOTH_100_BYTES 12580u
extern const uint8_t sprite_bluetooth_100[];

#define SPRITE_BLUETOOTH_50_WIDTH 50u
#define SPRITE_BLUETOOTH_50_HEIGHT 50u
#define SPRITE_BLUETOOTH_50_BYTES 3819u
extern const uint8_t sprite_bluetooth_50[];

#define SPRITE_BLUETOOTH_X_50_WIDTH 50u
#define SPRITE_BLUETOOTH_X_50_HEIGHT 50u
#define SPRITE_BLUETOOTH_X_50_BYTES 4027u
extern const uint8_t sprite_bluetooth_x_50[];

#define SPRITE_BRIEFCASE_100_WIDTH 100u
#define SPRITE_BRIEFCASE_100_HEIGHT 100u
#define SPRITE_BRIEFCASE_100_BYTES 12703u
extern const uint8_t sprite_briefcase_100[];

#define SPRITE_BTPAIRING_100_WIDTH 100u
#define SPRITE_BTPAIRING_100_HEIGHT 100u
#define SPRITE_BTPAIRING_100_BYTES 12906u
extern const uint8_t sprite_btpairing_100[];

#define SPRITE_CANCEL_60_WIDTH 60u
#define SPRITE_CANCEL_60_HEIGHT 60u
#define SPRITE_CANCEL_60_BYTES 6323u
extern const uint8_t sprite_cancel_60[];

#define SPRITE_CANCELDOOR_50_WIDTH 50u
#define SPRITE_CANCELDOOR_50_HEIGHT 50u
#define SPRITE_CANCELDOOR_50_BYTES 4075u
extern const uint8_t sprite_canceldoor_50[];

#define SPRITE_CLOUDUPLOAD_100_WIDTH 100u
#define SPRITE_CLOUDUPLOAD_100_HEIGHT 100u
#define SPRITE_CLOUDUPLOAD_100_BYTES 11111u
extern const uint8_t sprite_cloudupload_100[];

#define SPRITE_DEFAULT_WIFI_AP_WIDTH 100u
#define SPRITE_DEFAULT_WIFI_AP_HEIGHT 100u
#define SPRITE_DEFAULT_WIFI_AP_BYTES 13728u
extern const uint8_t sprite_default_wifi_ap[];

#define SPRITE_DOORRETURN_50_WIDTH 50u
#define SPRITE_DOORRETURN_50_HEIGHT 50u
#define SPRITE_DOORRETURN_50_BYTES 3491u
extern const uint8_t sprite_doorreturn_50[];

#define SPRITE_ENROLL_50_WIDTH 50u
#define SPRITE_ENROLL_50_HEIGHT 50u
#define SPRITE_ENROLL_50_BYTES 3541u
extern const uint8_t sprite_enroll_50[];

#define SPRITE_EYE_CLOSED_100_WIDTH 100u
#define SPRITE_EYE_CLOSED_100_HEIGHT 100u
#define SPRITE_EYE_CLOSED_100_BYTES 8986u
extern const uint8_t sprite_eye_closed_100[];

#define SPRITE_EYE_OPEN_100_WIDTH 100u
#define SPRITE_EYE_OPEN_100_HEIGHT 100u
#define SPRITE_EYE_OPEN_100_BYTES 7997u
extern const uint8_t sprite_eye_open_100[];

#define SPRITE_FIRSTAIDWIFI_50_WIDTH 50u
#define SPRITE_FIRSTAIDWIFI_50_HEIGHT 50u
#define SPRITE_FIRSTAIDWIFI_50_BYTES 3575u
extern const uint8_t sprite_firstaidwifi_50[];

#define SPRITE_FWUPDATE_WIDTH 100u
#define SPRITE_FWUPDATE_HEIGHT 100u
#define SPRITE_FWUPDATE_BYTES 12806u
extern const uint8_t sprite_fwupdate[];

#define SPRITE_GENERICDEVICE_BT_100_WIDTH 100u
#define SPRITE_GENERICDEVICE_BT_100_HEIGHT 100u
#define SPRITE_GENERICDEVICE_BT_100_BYTES 14595u
extern const uint8_t sprite_genericdevice_bt_100[];

#define SPRITE_GENERICWIFIROUTER_100_WIDTH 100u
#define SPRITE_GENERICWIFIROUTER_100_HEIGHT 100u
#define SPRITE_GENERICWIFIROUTER_100_BYTES 13164u
extern const uint8_t sprite_genericwifirouter_100[];

#define SPRITE_GREENCHECKMARK_100_WIDTH 100u
#define SPRITE_GREENCHECKMARK_100_HEIGHT 100u
#define SPRITE_GREENCHECKMARK_100_BYTES 8216u
extern const uint8_t sprite_greencheckmark_100[];

#define SPRITE_GREENCHECKMARK_50_WIDTH 50u
#define SPRITE_GREENCHECKMARK_50_HEIGHT 50u
#define SPRITE_GREENCHECKMARK_50_BYTES 2854u
extern const uint8_t sprite_greencheckmark_50[];

#define SPRITE_HOME_100_WIDTH 100u
#define SPRITE_HOME_100_HEIGHT 100u
#define SPRITE_HOME_100_BYTES 13241u
extern const uint8_t sprite_home_100[];

#define SPRITE_HOME_WIFI_100_WIDTH 100u
#define SPRITE_HOME_WIFI_100_HEIGHT 100u
#define SPRITE_HOME_WIFI_100_BYTES 14471u
extern const uint8_t sprite_home_wifi_100[];

#define SPRITE_HOURGLASS_30_OVERLAY_WIDTH 30u
#define SPRITE_HOURGLASS_30_OVERLAY_HEIGHT 30u
#define SPRITE_HOURGLASS_30_OVERLAY_BYTES 2259u
extern const uint8_t sprite_hourglass_30_overlay[];

#define SPRITE_HOURGLASS_60_1_WIDTH 60u
#define SPRITE_HOURGLASS_60_1_HEIGHT 60u
#define SPRITE_HOURGLASS_60_1_BYTES 4260u
extern const uint8_t sprite_hourglass_60_1[];

#define SPRITE_HOURGLASS_60_2_WIDTH 60u
#define SPRITE_HOURGLASS_60_2_HEIGHT 60u
#define SPRITE_HOURGLASS_60_2_BYTES 4189u
extern const uint8_t sprite_hourglass_60_2[];

#define SPRITE_HOURGLASS_60_3_WIDTH 60u
#define SPRITE_HOURGLASS_60_3_HEIGHT 60u
#define SPRITE_HOURGLASS_60_3_BYTES 4236u
extern const uint8_t sprite_hourglass_60_3[];

#define SPRITE_INFO_100_WIDTH 100u
#define SPRITE_INFO_100_HEIGHT 100u
#define SPRITE_INFO_100_BYTES 16257u
extern const uint8_t sprite_info_100[];

#define SPRITE_LAPTOP_100_WIDTH 100u
#define SPRITE_LAPTOP_100_HEIGHT 100u
#define SPRITE_LAPTOP_100_BYTES 9336u
extern const uint8_t sprite_laptop_100[];

#define SPRITE_LAPTOP_BT_100_WIDTH 100u
#define SPRITE_LAPTOP_BT_100_HEIGHT 100u
#define SPRITE_LAPTOP_BT_100_BYTES 10462u
extern const uint8_t sprite_laptop_bt_100[];

#define SPRITE_LOWBATT_100_WIDTH 100u
#define SPRITE_LOWBATT_100_HEIGHT 100u
#define SPRITE_LOWBATT_100_BYTES 8044u
extern const uint8_t sprite_lowbatt_100[];

#define SPRITE_MEMO_100_WIDTH 100u
#define SPRITE_MEMO_100_HEIGHT 100u
#define SPRITE_MEMO_100_BYTES 14449u
extern const uint8_t sprite_memo_100[];

#define SPRITE_MEMOTYPE_IDEA_WIDTH 50u
#define SPRITE_MEMOTYPE_IDEA_HEIGHT 50u
#define SPRITE_MEMOTYPE_IDEA_BYTES 3809u
extern const uint8_t sprite_memotype_idea[];

#define SPRITE_MEMOTYPE_JOURNAL_WIDTH 50u
#define SPRITE_MEMOTYPE_JOURNAL_HEIGHT 50u
#define SPRITE_MEMOTYPE_JOURNAL_BYTES 3513u
extern const uint8_t sprite_memotype_journal[];

#define SPRITE_MEMOTYPE_NOTE_WIDTH 50u
#define SPRITE_MEMOTYPE_NOTE_HEIGHT 50u
#define SPRITE_MEMOTYPE_NOTE_BYTES 3981u
extern const uint8_t sprite_memotype_note[];

#define SPRITE_MEMOTYPE_REMINDER_WIDTH 50u
#define SPRITE_MEMOTYPE_REMINDER_HEIGHT 50u
#define SPRITE_MEMOTYPE_REMINDER_BYTES 4521u
extern const uint8_t sprite_memotype_reminder[];

#define SPRITE_MEMOTYPE_TODO_WIDTH 50u
#define SPRITE_MEMOTYPE_TODO_HEIGHT 50u
#define SPRITE_MEMOTYPE_TODO_BYTES 3811u
extern const uint8_t sprite_memotype_todo[];

#define SPRITE_MIC_100_WIDTH 100u
#define SPRITE_MIC_100_HEIGHT 100u
#define SPRITE_MIC_100_BYTES 10530u
extern const uint8_t sprite_mic_100[];

#define SPRITE_MUTED_X_WIDTH 40u
#define SPRITE_MUTED_X_HEIGHT 40u
#define SPRITE_MUTED_X_BYTES 2315u
extern const uint8_t sprite_muted_x[];

#define SPRITE_NTPSYNC_100_WIDTH 100u
#define SPRITE_NTPSYNC_100_HEIGHT 100u
#define SPRITE_NTPSYNC_100_BYTES 11983u
extern const uint8_t sprite_ntpsync_100[];

#define SPRITE_OVERLAY_AIRPLANE_50_WIDTH 50u
#define SPRITE_OVERLAY_AIRPLANE_50_HEIGHT 50u
#define SPRITE_OVERLAY_AIRPLANE_50_BYTES 3926u
extern const uint8_t sprite_overlay_airplane_50[];

#define SPRITE_OVERLAY_BIRD_50_WIDTH 50u
#define SPRITE_OVERLAY_BIRD_50_HEIGHT 50u
#define SPRITE_OVERLAY_BIRD_50_BYTES 4143u
extern const uint8_t sprite_overlay_bird_50[];

#define SPRITE_OVERLAY_BRIEFCASE_50_WIDTH 50u
#define SPRITE_OVERLAY_BRIEFCASE_50_HEIGHT 50u
#define SPRITE_OVERLAY_BRIEFCASE_50_BYTES 3963u
extern const uint8_t sprite_overlay_briefcase_50[];

#define SPRITE_OVERLAY_BT_50_WIDTH 50u
#define SPRITE_OVERLAY_BT_50_HEIGHT 50u
#define SPRITE_OVERLAY_BT_50_BYTES 4126u
extern const uint8_t sprite_overlay_bt_50[];

#define SPRITE_OVERLAY_CAR_50_WIDTH 50u
#define SPRITE_OVERLAY_CAR_50_HEIGHT 50u
#define SPRITE_OVERLAY_CAR_50_BYTES 2849u
extern const uint8_t sprite_overlay_car_50[];

#define SPRITE_OVERLAY_CAT_50_WIDTH 50u
#define SPRITE_OVERLAY_CAT_50_HEIGHT 50u
#define SPRITE_OVERLAY_CAT_50_BYTES 5974u
extern const uint8_t sprite_overlay_cat_50[];

#define SPRITE_OVERLAY_CIRCLE_40_WIDTH 40u
#define SPRITE_OVERLAY_CIRCLE_40_HEIGHT 40u
#define SPRITE_OVERLAY_CIRCLE_40_BYTES 2847u
extern const uint8_t sprite_overlay_circle_40[];

#define SPRITE_OVERLAY_CLOUD_50_WIDTH 50u
#define SPRITE_OVERLAY_CLOUD_50_HEIGHT 50u
#define SPRITE_OVERLAY_CLOUD_50_BYTES 3299u
extern const uint8_t sprite_overlay_cloud_50[];

#define SPRITE_OVERLAY_DOG_50_WIDTH 50u
#define SPRITE_OVERLAY_DOG_50_HEIGHT 50u
#define SPRITE_OVERLAY_DOG_50_BYTES 4944u
extern const uint8_t sprite_overlay_dog_50[];

#define SPRITE_OVERLAY_HOME_50_WIDTH 50u
#define SPRITE_OVERLAY_HOME_50_HEIGHT 50u
#define SPRITE_OVERLAY_HOME_50_BYTES 4166u
extern const uint8_t sprite_overlay_home_50[];

#define SPRITE_OVERLAY_SQUARE_40_WIDTH 40u
#define SPRITE_OVERLAY_SQUARE_40_HEIGHT 40u
#define SPRITE_OVERLAY_SQUARE_40_BYTES 2262u
extern const uint8_t sprite_overlay_square_40[];

#define SPRITE_OVERLAY_TRIANGLE_40_WIDTH 40u
#define SPRITE_OVERLAY_TRIANGLE_40_HEIGHT 40u
#define SPRITE_OVERLAY_TRIANGLE_40_BYTES 2097u
extern const uint8_t sprite_overlay_triangle_40[];

#define SPRITE_OVERLAY_WIFI_50_WIDTH 50u
#define SPRITE_OVERLAY_WIFI_50_HEIGHT 50u
#define SPRITE_OVERLAY_WIFI_50_BYTES 3385u
extern const uint8_t sprite_overlay_wifi_50[];

#define SPRITE_PICKUP_WIDTH 50u
#define SPRITE_PICKUP_HEIGHT 50u
#define SPRITE_PICKUP_BYTES 2550u
extern const uint8_t sprite_pickup[];

#define SPRITE_PICKUPDOWN_WIDTH 50u
#define SPRITE_PICKUPDOWN_HEIGHT 50u
#define SPRITE_PICKUPDOWN_BYTES 2727u
extern const uint8_t sprite_pickupdown[];

#define SPRITE_SECURITY_50_WIDTH 50u
#define SPRITE_SECURITY_50_HEIGHT 50u
#define SPRITE_SECURITY_50_BYTES 4765u
extern const uint8_t sprite_security_50[];

#define SPRITE_SLEEP_100_WIDTH 100u
#define SPRITE_SLEEP_100_HEIGHT 100u
#define SPRITE_SLEEP_100_BYTES 12460u
extern const uint8_t sprite_sleep_100[];

#define SPRITE_SMARTPHONE_100_WIDTH 100u
#define SPRITE_SMARTPHONE_100_HEIGHT 100u
#define SPRITE_SMARTPHONE_100_BYTES 10922u
extern const uint8_t sprite_smartphone_100[];

#define SPRITE_SMARTPHONE_AP_100_WIDTH 100u
#define SPRITE_SMARTPHONE_AP_100_HEIGHT 100u
#define SPRITE_SMARTPHONE_AP_100_BYTES 12364u
extern const uint8_t sprite_smartphone_ap_100[];

#define SPRITE_SMARTPHONE_BT_100_WIDTH 100u
#define SPRITE_SMARTPHONE_BT_100_HEIGHT 100u
#define SPRITE_SMARTPHONE_BT_100_BYTES 12393u
extern const uint8_t sprite_smartphone_bt_100[];

#define SPRITE_SPEAKER_100_WIDTH 100u
#define SPRITE_SPEAKER_100_HEIGHT 100u
#define SPRITE_SPEAKER_100_BYTES 11664u
extern const uint8_t sprite_speaker_100[];

#define SPRITE_SPLASH_WIDTH 320u
#define SPRITE_SPLASH_HEIGHT 240u
#define SPRITE_SPLASH_BYTES 48938u
extern const uint8_t sprite_splash[];

#define SPRITE_TABLET_100_WIDTH 100u
#define SPRITE_TABLET_100_HEIGHT 100u
#define SPRITE_TABLET_100_BYTES 10864u
extern const uint8_t sprite_tablet_100[];

#define SPRITE_TABLET_BT_100_WIDTH 100u
#define SPRITE_TABLET_BT_100_HEIGHT 100u
#define SPRITE_TABLET_BT_100_BYTES 12365u
extern const uint8_t sprite_tablet_bt_100[];

#define SPRITE_THUMBSUP_100_WIDTH 100u
#define SPRITE_THUMBSUP_100_HEIGHT 100u
#define SPRITE_THUMBSUP_100_BYTES 13906u
extern const uint8_t sprite_thumbsup_100[];

#define SPRITE_TRASH_100_WIDTH 100u
#define SPRITE_TRASH_100_HEIGHT 100u
#define SPRITE_TRASH_100_BYTES 12067u
extern const uint8_t sprite_trash_100[];

#define SPRITE_TRASH_50_WIDTH 50u
#define SPRITE_TRASH_50_HEIGHT 50u
#define SPRITE_TRASH_50_BYTES 3604u
extern const uint8_t sprite_trash_50[];

#define SPRITE_UNKNOWN_BT_100_WIDTH 100u
#define SPRITE_UNKNOWN_BT_100_HEIGHT 100u
#define SPRITE_UNKNOWN_BT_100_BYTES 15032u
extern const uint8_t sprite_unknown_bt_100[];

#define SPRITE_WARNING_100_WIDTH 100u
#define SPRITE_WARNING_100_HEIGHT 100u
#define SPRITE_WARNING_100_BYTES 12579u
extern const uint8_t sprite_warning_100[];

#define SPRITE_WIFI_100_WIDTH 100u
#define SPRITE_WIFI_100_HEIGHT 100u
#define SPRITE_WIFI_100_BYTES 10195u
extern const uint8_t sprite_wifi_100[];

#define SPRITE_WIFI_50_WIDTH 50u
#define SPRITE_WIFI_50_HEIGHT 50u
#define SPRITE_WIFI_50_BYTES 3185u
extern const uint8_t sprite_wifi_50[];

#define SPRITE_WIFISEARCH_100_WIDTH 100u
#define SPRITE_WIFISEARCH_100_HEIGHT 100u
#define SPRITE_WIFISEARCH_100_BYTES 12084u
extern const uint8_t sprite_wifisearch_100[];

#define SPRITE_XCIRCLE_100_WIDTH 100u
#define SPRITE_XCIRCLE_100_HEIGHT 100u
#define SPRITE_XCIRCLE_100_BYTES 16739u
extern const uint8_t sprite_xcircle_100[];
