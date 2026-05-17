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
#include "thefly_utils.h"
#include "dbg_log.h"

// extern/global variables and functions go here

extern uint32_t reset_flag;
extern uint32_t reset_magic;
extern bool reset_was_magic;
