#pragma once

#include "thefly_common.h"
#include <stddef.h>
#include <stdint.h>

namespace Aegis
{

static constexpr size_t kMasterKeySize = 32;
static constexpr size_t kSha256Size    = 32;
static constexpr uint8_t kSalt[] = {
    0x98, 0xC2, 0x5A, 0xF2, 0xB7, 0x0F, 0xA4, 0xB3,
    0x42, 0xB4, 0x64, 0xE5, 0xEE, 0xD6, 0xFF, 0x3D,
    0x0D, 0xD8, 0x21, 0x9C, 0x9D, 0x7B, 0x16, 0xB4,
    0xCE, 0xDE, 0xCF, 0xFA, 0xCA, 0x4E, 0xF3, 0x2F,
};
static constexpr size_t kSaltSize = sizeof(kSalt);
static constexpr uint32_t kPbkdfIterations = 10000;

uint32_t rand();

bool init();
bool deinit();
bool isInitialized();
bool hasMasterKey();

const uint8_t* getMasterKey();
bool setMasterKey(const uint8_t* key);
#ifdef BUILD_IS_DEBUG
void setTestTempMasterKey(const uint8_t* key);
void setTestTempPassword(const uint8_t* password);
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
