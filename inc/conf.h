#pragma once

#include <cstdint>

/*
use this file for preprocessor definitions that are used to configure parts of the code at compile-time
*/

constexpr uint64_t kHalfGiB = 512ULL * 1024ULL * 1024ULL;
constexpr uint64_t kMaxGrowFileBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
