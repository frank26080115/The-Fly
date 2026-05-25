#pragma once

#include "thefly_common.h"
#include <stddef.h>
#include <stdint.h>

namespace Aegis
{

static constexpr size_t kFilecryptKeySize = 32;
static constexpr size_t kNetworkKeySize   = 32;
static constexpr size_t kSha256Size       = 32;
static constexpr uint8_t kSaltFilecrypt[] = {
    0x98, 0xC2, 0x5A, 0xF2, 0xB7, 0x0F, 0xA4, 0xB3,
    0x42, 0xB4, 0x64, 0xE5, 0xEE, 0xD6, 0xFF, 0x3D,
    0x0D, 0xD8, 0x21, 0x9C, 0x9D, 0x7B, 0x16, 0xB4,
    0xCE, 0xDE, 0xCF, 0xFA, 0xCA, 0x4E, 0xF3, 0x2F,
};
static constexpr uint8_t kSaltNetwork[] = {
    0x36, 0x22, 0x5C, 0x86, 0xC1, 0x70, 0xF2, 0xC1,
    0x11, 0xD3, 0xD6, 0xDF, 0x57, 0x6E, 0x28, 0x93,
    0x71, 0x4E, 0x52, 0x6C, 0xD8, 0x43, 0xB2, 0x6B,
    0xDF, 0xEE, 0x1F, 0x12, 0xC9, 0xA3, 0xE5, 0xB2,
};
static constexpr size_t kSaltSize = 32;
static constexpr size_t kSaltFilecryptSize = sizeof(kSaltFilecrypt);
static constexpr size_t kSaltNetworkSize   = sizeof(kSaltNetwork);
static constexpr uint32_t kPbkdfIterations = 100000;

uint32_t rand();

bool init();
bool deinit();
bool isInitialized();
bool hasMasterKey();
bool hasFilecryptKey();
bool hasNetworkKey();

const uint8_t* getFilecryptKey();
const uint8_t* getNetworkKey();
bool setFilecryptKey(const uint8_t* key);
bool setNetworkKey(const uint8_t* key);
#ifdef BUILD_IS_DEBUG
void setTestTempFilecryptKey(const uint8_t* key);
void setTestTempNetworkKey(const uint8_t* key);
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
