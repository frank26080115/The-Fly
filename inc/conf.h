#pragma once

#include <cstdint>

/*
use this file for preprocessor definitions that are used to configure parts of the code at compile-time
*/

#define BUILD_WITH_SECURITY

#if (defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > ESP_LOG_ERROR) || (defined(LOG_LOCAL_LEVEL) && LOG_LOCAL_LEVEL > ESP_LOG_ERROR)
#ifndef BUILD_IS_DEBUG
#define BUILD_IS_DEBUG
#endif
#endif

constexpr uint64_t kHalfGiB = 512ULL * 1024ULL * 1024ULL;
constexpr uint64_t kMaxGrowFileBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;

// #define ENABLE_FILE_PREALLOCATION
// #define ENABLE_HFP_AUDIO_DIAGNOSTICS

