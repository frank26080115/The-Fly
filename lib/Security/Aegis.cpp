/*
Security related code
A few wrappers for cryptographic functions
Manages memory for cryptographic keys
*/

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "Aegis.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "dbg_log.h"
#include "esp_flash_encrypt.h"
#include "esp_random.h"
#include "esp_secure_boot.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/version.h"
#include "nvs.h"
#include "PinCode.h"

namespace Aegis
{
namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr const char* TAG                   = "Aegis";
constexpr const char* kNvsNamespace         = "aegis";
constexpr const char* kFilecryptKeyBlobName = "filecrypt_key";
constexpr const char* kNetworkKeyBlobName   = "network_key";

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

uint8_t g_filecrypt_key[kFilecryptKeySize] = {};
uint8_t g_network_key[kNetworkKeySize]     = {};
uint8_t g_network_config_hash[kSha1Size]   = {};
bool    g_initialized                      = false;
bool    g_filecrypt_key_valid              = false;
bool    g_network_key_valid                = false;
bool    g_network_config_hash_valid        = false;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

void     clear_filecrypt_key();
void     clear_network_key();
void     clear_network_config_hash();
void     clear_all_keys();
bool     valid_buffer(const uint8_t* ptr, size_t len);
bool     sha1_update_buffer(mbedtls_md_context_t& ctx, const void* data, size_t data_len);
bool     tamper_evidence_hash_from_network_hash(const uint8_t network_config_hash[kSha1Size], uint8_t out[kSha1Size]);
uint32_t sha1_prefix_u32(const uint8_t digest[kSha1Size]);
bool     load_key_from_nvs(nvs_handle_t handle, const char* blob_name, uint8_t* key, size_t key_size, bool& valid);
bool     save_key_to_nvs(const char* blob_name, const uint8_t* key, size_t key_size);
bool     save_keys_to_nvs(const uint8_t* filecrypt_key, const uint8_t* network_key);
void     pbkdf2_yield(uint32_t completed_rounds);
bool     pbkdf2_block_hmac_sha256(const uint8_t* password,
                                  size_t         password_len,
                                  const uint8_t* salt,
                                  size_t         salt_len,
                                  uint32_t       iterations,
                                  uint32_t       block_index,
                                  uint8_t        out[kSha256Size]);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

uint32_t rand()
{
    return esp_random() ^ static_cast<uint32_t>(millis()) ^ static_cast<uint32_t>(micros());
}

bool init()
{
    if (g_initialized)
    {
        return PinCode::init();
    }

#ifdef TEST_MOCK_MASTER_KEY
    g_filecrypt_key_valid = true;
    g_network_key_valid   = true;
    g_initialized         = true;
    DBG_LOGI(TAG, "NVS Aegis using TEST_MOCK_MASTER_KEY");
    return PinCode::init();
#endif

    nvs_handle_t    handle   = 0;
    const esp_err_t open_err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (open_err == ESP_ERR_NVS_NOT_FOUND)
    {
        clear_all_keys();
        g_initialized = true;
        DBG_LOGI(TAG, "NVS Aegis namespace not found; keys unavailable");
        return PinCode::init();
    }
    if (open_err != ESP_OK)
    {
        DBG_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(open_err));
        clear_all_keys();
        return false;
    }

    load_key_from_nvs(handle, kFilecryptKeyBlobName, g_filecrypt_key, sizeof(g_filecrypt_key), g_filecrypt_key_valid);
    load_key_from_nvs(handle, kNetworkKeyBlobName, g_network_key, sizeof(g_network_key), g_network_key_valid);
    nvs_close(handle);

    g_initialized = true;
    return PinCode::init();
}

bool deinit()
{
    clear_all_keys();
    clear_network_config_hash();
    g_initialized = false;
    return true;
}

bool isInitialized()
{
    return g_initialized;
}

bool hasMasterKey()
{
    return hasFilecryptKey() && hasNetworkKey();
}

bool hasFilecryptKey()
{
    return g_initialized && g_filecrypt_key_valid;
}

bool hasNetworkKey()
{
    return g_initialized && g_network_key_valid;
}

bool isNvsEncrypted()
{
    // NOTE: we are depending on full flash encryption, not just encrypted NVS alone
    return esp_flash_encryption_enabled();
}

bool isFwUpdateSecure()
{
    return esp_secure_boot_enabled();
}

const uint8_t* getFilecryptKey()
{
    return hasFilecryptKey() ? g_filecrypt_key : nullptr;
}

const uint8_t* getNetworkKey()
{
    return hasNetworkKey() ? g_network_key : nullptr;
}

bool sha1(const void* data, size_t data_len, uint8_t out[kSha1Size])
{
    static constexpr uint8_t kEmptyBuffer = 0;
    const uint8_t*           bytes        = static_cast<const uint8_t*>(data);
    if (!out || !valid_buffer(bytes, data_len))
    {
        return false;
    }
    if (!bytes)
    {
        bytes = &kEmptyBuffer;
    }

    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (!md_info)
    {
        mbedtls_platform_zeroize(out, kSha1Size);
        return false;
    }

    const bool ok = mbedtls_md(md_info, bytes, data_len, out) == 0;
    if (!ok)
    {
        mbedtls_platform_zeroize(out, kSha1Size);
    }
    return ok;
}

bool networkConfigHash(const void* network_config, size_t network_config_size, uint8_t out[kSha1Size])
{
    return sha1(network_config, network_config_size, out);
}

bool cacheNetworkConfigHash(const void* network_config, size_t network_config_size)
{
    uint8_t hash[kSha1Size] = {};
    if (!networkConfigHash(network_config, network_config_size, hash))
    {
        clear_network_config_hash();
        return false;
    }

    memcpy(g_network_config_hash, hash, sizeof(g_network_config_hash));
    g_network_config_hash_valid = true;
    mbedtls_platform_zeroize(hash, sizeof(hash));
    return true;
}

bool getCachedNetworkConfigHash(uint8_t out[kSha1Size])
{
    if (!out || !g_network_config_hash_valid)
    {
        return false;
    }

    memcpy(out, g_network_config_hash, sizeof(g_network_config_hash));
    return true;
}

bool tamperEvidenceHash(const void* network_config, size_t network_config_size, uint8_t out[kSha1Size])
{
    uint8_t network_hash[kSha1Size] = {};
    if (!networkConfigHash(network_config, network_config_size, network_hash))
    {
        return false;
    }

    const bool ok = tamper_evidence_hash_from_network_hash(network_hash, out);
    mbedtls_platform_zeroize(network_hash, sizeof(network_hash));
    return ok;
}

bool tamperEvidenceHash(uint8_t out[kSha1Size])
{
    if (!g_network_config_hash_valid)
    {
        return false;
    }

    return tamper_evidence_hash_from_network_hash(g_network_config_hash, out);
}

bool tamperEvidenceCode(const void* network_config, size_t network_config_size, uint32_t& out)
{
    uint8_t hash[kSha1Size] = {};
    if (!tamperEvidenceHash(network_config, network_config_size, hash))
    {
        out = 0;
        return false;
    }

    out = sha1_prefix_u32(hash);
    mbedtls_platform_zeroize(hash, sizeof(hash));
    return true;
}

bool tamperEvidenceCode(uint32_t& out)
{
    uint8_t hash[kSha1Size] = {};
    if (!tamperEvidenceHash(hash))
    {
        out = 0;
        return false;
    }

    out = sha1_prefix_u32(hash);
    mbedtls_platform_zeroize(hash, sizeof(hash));
    return true;
}

#ifdef TEST_MOCK_PASSWORD
void setTestTempFilecryptKey(const uint8_t* key)
{
    if (!key)
    {
        clear_filecrypt_key();
        return;
    }

    memcpy(g_filecrypt_key, key, sizeof(g_filecrypt_key));
    g_filecrypt_key_valid = true;
    g_initialized         = true;
}

void setTestTempNetworkKey(const uint8_t* key)
{
    if (!key)
    {
        clear_network_key();
        return;
    }

    memcpy(g_network_key, key, sizeof(g_network_key));
    g_network_key_valid = true;
    g_initialized       = true;
}

void setTestTempPassword(const uint8_t* pw)
{
    if (!pw)
    {
        clear_all_keys();
        return;
    }

    uint8_t      filecrypt_key[kFilecryptKeySize] = {};
    uint8_t      network_key[kNetworkKeySize]     = {};
    const size_t password_len                     = strlen(reinterpret_cast<const char*>(pw));
    const bool   filecrypt_ok                     = pbkdf2HmacSha256(pw,
                                                                     password_len,
                                                                     kSaltFilecrypt,
                                                                     kSaltFilecryptSize,
                                                                     kPbkdfIterations,
                                                                     filecrypt_key,
                                                                     sizeof(filecrypt_key));
    const bool   network_ok                       = pbkdf2HmacSha256(pw,
                                                                     password_len,
                                                                     kSaltNetwork,
                                                                     kSaltNetworkSize,
                                                                     kPbkdfIterations,
                                                                     network_key,
                                                                     sizeof(network_key));
    if (filecrypt_ok && network_ok)
    {
        setTestTempFilecryptKey(filecrypt_key);
        setTestTempNetworkKey(network_key);
    }
    else
    {
        clear_all_keys();
    }
    mbedtls_platform_zeroize(filecrypt_key, sizeof(filecrypt_key));
    mbedtls_platform_zeroize(network_key, sizeof(network_key));
}
#endif

bool setFilecryptKey(const uint8_t* key)
{
    if (!key)
    {
        return false;
    }
    if (!g_initialized && !init())
    {
        return false;
    }

    uint8_t staged_key[kFilecryptKeySize] = {};
    memcpy(staged_key, key, sizeof(staged_key));

    if (!save_key_to_nvs(kFilecryptKeyBlobName, staged_key, sizeof(staged_key)))
    {
        mbedtls_platform_zeroize(staged_key, sizeof(staged_key));
        return false;
    }

    memcpy(g_filecrypt_key, staged_key, sizeof(g_filecrypt_key));
    g_filecrypt_key_valid = true;
    mbedtls_platform_zeroize(staged_key, sizeof(staged_key));
    return true;
}

bool setNetworkKey(const uint8_t* key)
{
    if (!key)
    {
        return false;
    }
    if (!g_initialized && !init())
    {
        return false;
    }

    uint8_t staged_key[kNetworkKeySize] = {};
    memcpy(staged_key, key, sizeof(staged_key));

    if (!save_key_to_nvs(kNetworkKeyBlobName, staged_key, sizeof(staged_key)))
    {
        mbedtls_platform_zeroize(staged_key, sizeof(staged_key));
        return false;
    }

    memcpy(g_network_key, staged_key, sizeof(g_network_key));
    g_network_key_valid = true;
    mbedtls_platform_zeroize(staged_key, sizeof(staged_key));
    return true;
}

bool setMasterKeys(const uint8_t* filecrypt_key, const uint8_t* network_key)
{
    if (!filecrypt_key || !network_key)
    {
        return false;
    }
    if (!g_initialized && !init())
    {
        return false;
    }

    uint8_t staged_filecrypt_key[kFilecryptKeySize] = {};
    uint8_t staged_network_key[kNetworkKeySize]     = {};
    memcpy(staged_filecrypt_key, filecrypt_key, sizeof(staged_filecrypt_key));
    memcpy(staged_network_key, network_key, sizeof(staged_network_key));

    if (!save_keys_to_nvs(staged_filecrypt_key, staged_network_key))
    {
        mbedtls_platform_zeroize(staged_filecrypt_key, sizeof(staged_filecrypt_key));
        mbedtls_platform_zeroize(staged_network_key, sizeof(staged_network_key));
        return false;
    }

    memcpy(g_filecrypt_key, staged_filecrypt_key, sizeof(g_filecrypt_key));
    memcpy(g_network_key, staged_network_key, sizeof(g_network_key));
    g_filecrypt_key_valid = true;
    g_network_key_valid   = true;
    mbedtls_platform_zeroize(staged_filecrypt_key, sizeof(staged_filecrypt_key));
    mbedtls_platform_zeroize(staged_network_key, sizeof(staged_network_key));
    return true;
}

bool setNetworkKeyAndGenerateFilecryptKey(const uint8_t* network_key)
{
    if (!network_key)
    {
        return false;
    }
    if (!g_initialized && !init())
    {
        return false;
    }

    uint8_t filecrypt_key[kFilecryptKeySize] = {};
    esp_fill_random(filecrypt_key, sizeof(filecrypt_key));
    const bool ok = setMasterKeys(filecrypt_key, network_key);
    mbedtls_platform_zeroize(filecrypt_key, sizeof(filecrypt_key));
    return ok;
}

bool generateFilecryptKey()
{
    if (!g_initialized && !init())
    {
        return false;
    }

    uint8_t key[kFilecryptKeySize] = {};
    esp_fill_random(key, sizeof(key));
    const bool ok = setFilecryptKey(key);
    mbedtls_platform_zeroize(key, sizeof(key));
    return ok;
}

bool hmacSha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len, uint8_t out[kSha256Size])
{
    if (!out || !valid_buffer(key, key_len) || !valid_buffer(data, data_len))
    {
        return false;
    }

    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info)
    {
        return false;
    }

    return mbedtls_md_hmac(md_info, key, key_len, data, data_len, out) == 0;
}

bool pbkdf2HmacSha256(const uint8_t* password,
                      size_t         password_len,
                      const uint8_t* salt,
                      size_t         salt_len,
                      uint32_t       iterations,
                      uint8_t*       out,
                      size_t         out_len)
{
    if (!out || out_len == 0 || iterations == 0 || !valid_buffer(password, password_len) ||
        !valid_buffer(salt, salt_len))
    {
        return false;
    }

    static constexpr uint8_t kEmptyBuffer   = 0;
    const uint8_t*           password_bytes = password ? password : &kEmptyBuffer;
    const uint8_t*           salt_bytes     = salt ? salt : &kEmptyBuffer;

    size_t   out_offset  = 0;
    uint32_t block_index = 1;
    while (out_offset < out_len)
    {
        uint8_t block[kSha256Size] = {};
        if (!pbkdf2_block_hmac_sha256(password_bytes,
                                      password_len,
                                      salt_bytes,
                                      salt_len,
                                      iterations,
                                      block_index,
                                      block))
        {
            mbedtls_platform_zeroize(out, out_len);
            return false;
        }

        const size_t remaining = out_len - out_offset;
        const size_t copy_len  = remaining < sizeof(block) ? remaining : sizeof(block);
        memcpy(out + out_offset, block, copy_len);
        out_offset += copy_len;
        mbedtls_platform_zeroize(block, sizeof(block));

        if (out_offset < out_len && block_index == UINT32_MAX)
        {
            mbedtls_platform_zeroize(out, out_len);
            return false;
        }
        ++block_index;
    }

    return true;
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void clear_filecrypt_key()
{
    mbedtls_platform_zeroize(g_filecrypt_key, sizeof(g_filecrypt_key));
    g_filecrypt_key_valid = false;
}

void clear_network_key()
{
    mbedtls_platform_zeroize(g_network_key, sizeof(g_network_key));
    g_network_key_valid = false;
}

void clear_network_config_hash()
{
    mbedtls_platform_zeroize(g_network_config_hash, sizeof(g_network_config_hash));
    g_network_config_hash_valid = false;
}

void clear_all_keys()
{
    clear_filecrypt_key();
    clear_network_key();
}

bool valid_buffer(const uint8_t* ptr, size_t len)
{
    return ptr || len == 0;
}

bool sha1_update_buffer(mbedtls_md_context_t& ctx, const void* data, size_t data_len)
{
    static constexpr uint8_t kEmptyBuffer = 0;
    const uint8_t*           bytes        = static_cast<const uint8_t*>(data);
    if (!valid_buffer(bytes, data_len))
    {
        return false;
    }
    if (!bytes)
    {
        bytes = &kEmptyBuffer;
    }
    return mbedtls_md_update(&ctx, bytes, data_len) == 0;
}

bool tamper_evidence_hash_from_network_hash(const uint8_t network_config_hash[kSha1Size], uint8_t out[kSha1Size])
{
    if (!network_config_hash || !out)
    {
        return false;
    }
    if (!g_initialized && !init())
    {
        return false;
    }
    if (!hasMasterKey())
    {
        return false;
    }

    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (!md_info)
    {
        return false;
    }

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    bool ok = mbedtls_md_setup(&ctx, md_info, 0) == 0 && mbedtls_md_starts(&ctx) == 0 &&
              sha1_update_buffer(ctx, g_filecrypt_key, sizeof(g_filecrypt_key)) &&
              sha1_update_buffer(ctx, g_network_key, sizeof(g_network_key)) &&
              sha1_update_buffer(ctx, network_config_hash, kSha1Size) && mbedtls_md_finish(&ctx, out) == 0;
    mbedtls_md_free(&ctx);

    if (!ok)
    {
        mbedtls_platform_zeroize(out, kSha1Size);
    }
    return ok;
}

uint32_t sha1_prefix_u32(const uint8_t digest[kSha1Size])
{
    return (static_cast<uint32_t>(digest[0]) << 24) | (static_cast<uint32_t>(digest[1]) << 16) |
           (static_cast<uint32_t>(digest[2]) << 8) | static_cast<uint32_t>(digest[3]);
}

bool load_key_from_nvs(nvs_handle_t handle, const char* blob_name, uint8_t* key, size_t key_size, bool& valid)
{
    valid = false;
    mbedtls_platform_zeroize(key, key_size);

    size_t          stored_size = key_size;
    const esp_err_t err         = nvs_get_blob(handle, blob_name, key, &stored_size);
    if (err == ESP_OK && stored_size == key_size)
    {
        valid = true;
        return true;
    }

    mbedtls_platform_zeroize(key, key_size);
    if (err != ESP_ERR_NVS_NOT_FOUND)
    {
        DBG_LOGW(TAG,
                 "NVS %s load failed: %s size=%u",
                 blob_name,
                 esp_err_to_name(err),
                 static_cast<unsigned>(stored_size));
    }
    return false;
}

bool save_key_to_nvs(const char* blob_name, const uint8_t* key, size_t key_size)
{
    nvs_handle_t handle = 0;
    esp_err_t    err    = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        DBG_LOGE(TAG, "NVS open for %s save failed: %s", blob_name, esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(handle, blob_name, key, key_size);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK)
    {
        DBG_LOGE(TAG, "NVS %s save failed: %s", blob_name, esp_err_to_name(err));
        return false;
    }

    return true;
}

bool save_keys_to_nvs(const uint8_t* filecrypt_key, const uint8_t* network_key)
{
    nvs_handle_t handle = 0;
    esp_err_t    err    = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        DBG_LOGE(TAG, "NVS open for key save failed: %s", esp_err_to_name(err));
        return false;
    }

    if (filecrypt_key)
    {
        err = nvs_set_blob(handle, kFilecryptKeyBlobName, filecrypt_key, kFilecryptKeySize);
    }
    if (err == ESP_OK && network_key)
    {
        err = nvs_set_blob(handle, kNetworkKeyBlobName, network_key, kNetworkKeySize);
    }
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK)
    {
        DBG_LOGE(TAG, "NVS key save failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

constexpr size_t   kPbkdf2BlockIndexSize = 4;
constexpr uint32_t kPbkdf2YieldInterval  = 256;

void pbkdf2_yield(uint32_t completed_rounds)
{
    if ((completed_rounds % kPbkdf2YieldInterval) == 0)
    {
        delay(1);
    }
}

bool pbkdf2_block_hmac_sha256(const uint8_t* password,
                              size_t         password_len,
                              const uint8_t* salt,
                              size_t         salt_len,
                              uint32_t       iterations,
                              uint32_t       block_index,
                              uint8_t        out[kSha256Size])
{
    if (salt_len > SIZE_MAX - kPbkdf2BlockIndexSize)
    {
        return false;
    }

    const size_t salt_block_len = salt_len + kPbkdf2BlockIndexSize;
    uint8_t*     salt_block     = static_cast<uint8_t*>(malloc(salt_block_len));
    if (!salt_block)
    {
        return false;
    }

    if (salt_len > 0)
    {
        memcpy(salt_block, salt, salt_len);
    }
    salt_block[salt_len]     = static_cast<uint8_t>((block_index >> 24) & 0xFF);
    salt_block[salt_len + 1] = static_cast<uint8_t>((block_index >> 16) & 0xFF);
    salt_block[salt_len + 2] = static_cast<uint8_t>((block_index >> 8) & 0xFF);
    salt_block[salt_len + 3] = static_cast<uint8_t>(block_index & 0xFF);

    uint8_t u[kSha256Size]      = {};
    uint8_t next_u[kSha256Size] = {};
    bool    ok                  = hmacSha256(password, password_len, salt_block, salt_block_len, u);
    free(salt_block);
    if (!ok)
    {
        mbedtls_platform_zeroize(u, sizeof(u));
        mbedtls_platform_zeroize(next_u, sizeof(next_u));
        return false;
    }

    memcpy(out, u, kSha256Size);
    for (uint32_t round = 1; round < iterations; ++round)
    {
        if (!hmacSha256(password, password_len, u, sizeof(u), next_u))
        {
            mbedtls_platform_zeroize(u, sizeof(u));
            mbedtls_platform_zeroize(next_u, sizeof(next_u));
            mbedtls_platform_zeroize(out, kSha256Size);
            return false;
        }
        memcpy(u, next_u, sizeof(u));
        for (size_t i = 0; i < kSha256Size; ++i)
        {
            out[i] ^= u[i];
        }
        pbkdf2_yield(round);
    }

    mbedtls_platform_zeroize(u, sizeof(u));
    mbedtls_platform_zeroize(next_u, sizeof(next_u));
    return true;
}
} // namespace

} // namespace Aegis
