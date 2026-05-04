#pragma once

#include <SdFat.h>
#include <stdint.h>

namespace SdCard
{

bool begin();
bool isReady();

SdFs& fs();

uint64_t totalBytes();
uint64_t usedBytes();
uint64_t freeBytes();

} // namespace SdCard
