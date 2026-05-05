#pragma once

#include <SdFat.h>
#include <stdint.h>

/*
This wrapper exists because we did a migration from using the SD library to using the SdFat library
and then some existing functionality needs to be re-implemented during the migration
 */
namespace MicroSdCard
{

bool begin();
bool isReady();

SdFs& fs();

uint64_t totalBytes();
uint64_t usedBytes();
uint64_t freeBytes();

} // namespace MicroSdCard
