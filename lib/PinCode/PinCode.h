#pragma once

#include "thefly_common.h"

#include <stdint.h>
#include <time.h>

#define PIN_CODE_MAX_BAD_ATTEMPTS_ALLOWED 10
#define PINCODE_MAX_BAD_ATTEMPTS_ALLOWED PIN_CODE_MAX_BAD_ATTEMPTS_ALLOWED
#define PINCODE_MAX_LENGTH 32
#define PINCODE_DEFAULT_LENGTH 6

typedef struct
{
    char pin_code[PINCODE_MAX_LENGTH]; // actual length depends on null termination
    bool active;
    uint32_t bad_attempts;
    uint32_t salt;
    time_t date_set;
    time_t date_revoked;
}
pin_code_cfg_t;

namespace PinCode
{

bool init();
bool logBadAttempt(uint32_t* failedAttempts = nullptr);
bool logSuccessfulUsage();
bool revokePin();
#if BUILD_WITH_SECURITY_LEVEL == 1
bool setPin(const char* pin);
#endif
bool regeneratePin();
const char* getPin();
bool getConfig(pin_code_cfg_t& out);

} // namespace PinCode
