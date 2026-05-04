#pragma once

/*
literally every source file probably includes this file
*/

#include <stdint.h>
#include <stdbool.h>
#include "esp_attr.h"

#include "conf.h"
#include "defs.h"
#include "pins.h"

// extern/global variables and functions go here

extern RTC_DATA_ATTR uint32_t reset_flag;
extern RTC_DATA_ATTR uint32_t reset_magic;
