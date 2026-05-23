#pragma once

#include "thefly_common.h"
#include <stddef.h>
#include <stdint.h>

namespace Aegis
{

static constexpr size_t kMasterKeySize = 32;
static constexpr size_t kSha256Size    = 32;

uint32_t rand();

bool init();
bool deinit();
bool isInitialized();
bool hasMasterKey();

const uint8_t* getMasterKey();
bool setMasterKey(const uint8_t* key);
#ifdef BUILD_IS_DEBUG
void setTestTempMasterKey(const uint8_t* key);
#endif

bool hmacSha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len, uint8_t out[kSha256Size]);
bool pbkdf2HmacSha256(const uint8_t* password,
                      size_t password_len,
                      const uint8_t* salt,
                      size_t salt_len,
                      uint32_t iterations,
                      uint8_t* out,
                      size_t out_len);

} // namespace Aegis
