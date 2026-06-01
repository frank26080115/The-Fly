#pragma once

#include "dbg_log.h"

#include <cstdint>

/*
use this file for preprocessor definitions that are used to configure parts of the code at compile-time
*/

//#define BUILD_CLOUD_FEATURES
//#define TEST_SIM_BATTERY
//#define TEST_BOOT_ERROR_NONFATAL
//#define TEST_BOOT_ERROR_FATAL
//#define TEST_MOCK_FW_UPDATE 1
//#define TEST_MOCK_MASTER_KEY
#define TEST_MOCK_PIN_CODE
#define TEST_MOCK_PASSWORD

#define BUILD_SILENCE_GAP_REMOVAL

#if !defined(BUILD_WITH_SECURITY_LEVEL)
#error BUILD_WITH_SECURITY_LEVEL must be defined!
#elif BUILD_WITH_SECURITY_LEVEL > 3
#error BUILD_WITH_SECURITY_LEVEL not valid
#endif

// storage stays the same, up to max, but for secure setups, only 1 is allowed, as we need to track enrollment
#ifdef BUILD_CLOUD_FEATURES
#define CLOUD_SERVER_CNT_MAX        8
#if BUILD_WITH_SECURITY_LEVEL >= 2
#define CLOUD_SERVER_CNT_ALLOWED    CLOUD_SERVER_CNT_MAX
#else
#define CLOUD_SERVER_CNT_ALLOWED    1
#endif
#else
#define CLOUD_SERVER_CNT_MAX        1
#define CLOUD_SERVER_CNT_ALLOWED    0
#endif

// storage stays the same, up to max, but allowed customizable soft-AP configurations varies based on security levels
#define SOFTAP_CUSTOM_CFG_CNT_MAX    4
#if BUILD_WITH_SECURITY_LEVEL <= 0
#define SOFTAP_CUSTOM_CFG_CNT        SOFTAP_CUSTOM_CFG_CNT_MAX
#elif BUILD_WITH_SECURITY_LEVEL == 1
#define SOFTAP_CUSTOM_CFG_CNT        1
#elif BUILD_WITH_SECURITY_LEVEL >= 2
#define SOFTAP_CUSTOM_CFG_CNT        0
#endif

#if BUILD_WITH_SECURITY_LEVEL == 1
#define BUILD_WITH_ENCRYPTED_PLAYBACK
#endif

#if BUILD_WITH_SECURITY_LEVEL >= 1
#define BUILD_WITH_DECRYPTED_DOWNLOAD
#endif

#define MOST_RECENT_FILES_MAX_FILES  20

// 8192 bytes is approximately 128ms worth of 16 kHz 16-bit stereo PCM data.
#define WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH 8192

#if defined(DBG_LOG_LOCAL_LEVEL) && DBG_LOG_LOCAL_LEVEL > DBG_LOG_ERROR
#ifndef BUILD_IS_DEBUG
#define BUILD_IS_DEBUG
#endif
#endif

// file preallocation constants, not used
constexpr uint64_t kHalfGiB = 512ULL * 1024ULL * 1024ULL;
constexpr uint64_t kMaxGrowFileBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;

// #define ENABLE_FILE_PREALLOCATION
// #define ENABLE_HFP_AUDIO_DIAGNOSTICS

