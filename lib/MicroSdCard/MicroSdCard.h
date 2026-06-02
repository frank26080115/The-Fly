#pragma once

#include "thefly_common.h"
#include <SdFat.h>
#include <stdint.h>

/*
This wrapper exists because we did a migration from using the SD library to using the SdFat library
and then some existing functionality needs to be re-implemented during the migration
 */
namespace MicroSdCard
{

enum class Health
{
    Ready,
    NotReady,
    MissingOrUnreadable,
    Full,
};

bool        begin();
bool        isReady();
Health      health();
const char* healthName(Health health);

SdFs& fs();

uint64_t totalBytes();
uint64_t usedBytes();
uint64_t freeBytes();

} // namespace MicroSdCard
