#include "Aegis.h"

#include <Arduino.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/version.h"
#include "nvs.h"

namespace Aegis
{
namespace
{

constexpr const char* TAG                 = "Aegis";
constexpr const char* kNvsNamespace       = "aegis";
constexpr const char* kMasterKeyBlobName  = "master_key";

nvs_handle_t g_nvs              = 0;
uint8_t      g_master_key[kMasterKeySize] = {};
bool         g_initialized      = false;
bool         g_master_key_valid = false;

void clear_master_key()
{
    mbedtls_platform_zeroize(g_master_key, sizeof(g_master_key));
    g_master_key_valid = false;
}

bool valid_buffer(const uint8_t* ptr, size_t len)
{
    return ptr || len == 0;
}

} // namespace

uint32_t rand()
{
    return esp_random() ^ static_cast<uint32_t>(millis()) ^ static_cast<uint32_t>(micros());
}

bool init()
{
    if (g_initialized)
    {
        return true;
    }

    const esp_err_t open_err = nvs_open(kNvsNamespace, NVS_READWRITE, &g_nvs);
    if (open_err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(open_err));
        g_nvs = 0;
        return false;
    }

    size_t key_size = sizeof(g_master_key);
    const esp_err_t get_err = nvs_get_blob(g_nvs, kMasterKeyBlobName, g_master_key, &key_size);
    if (get_err == ESP_OK && key_size == sizeof(g_master_key))
    {
        g_master_key_valid = true;
    }
    else
    {
        clear_master_key();
        if (get_err != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW(TAG, "NVS master key load failed: %s", esp_err_to_name(get_err));
        }
    }

    g_initialized = true;
    return true;
}

bool deinit()
{
    clear_master_key();
    if (g_initialized)
    {
        nvs_close(g_nvs);
    }
    g_nvs = 0;
    g_initialized = false;
    return true;
}

bool isInitialized()
{
    return g_initialized;
}

bool hasMasterKey()
{
    return g_initialized && g_master_key_valid;
}

const uint8_t* getMasterKey()
{
    return hasMasterKey() ? g_master_key : nullptr;
}

bool setMasterKey(const uint8_t* key)
{
    if (!key)
    {
        return false;
    }
    if (!g_initialized && !init())
    {
        return false;
    }

    uint8_t staged_key[kMasterKeySize] = {};
    memcpy(staged_key, key, sizeof(staged_key));

    esp_err_t err = nvs_set_blob(g_nvs, kMasterKeyBlobName, staged_key, sizeof(staged_key));
    if (err == ESP_OK)
    {
        err = nvs_commit(g_nvs);
    }
    if (err != ESP_OK)
    {
        mbedtls_platform_zeroize(staged_key, sizeof(staged_key));
        ESP_LOGE(TAG, "NVS master key save failed: %s", esp_err_to_name(err));
        return false;
    }

    memcpy(g_master_key, staged_key, sizeof(g_master_key));
    g_master_key_valid = true;
    mbedtls_platform_zeroize(staged_key, sizeof(staged_key));
    return true;
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
                      size_t password_len,
                      const uint8_t* salt,
                      size_t salt_len,
                      uint32_t iterations,
                      uint8_t* out,
                      size_t out_len)
{
    if (!out || out_len == 0 || iterations == 0 || !valid_buffer(password, password_len) || !valid_buffer(salt, salt_len))
    {
        return false;
    }

#if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER >= 0x03000000
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, password, password_len, salt, salt_len, iterations, out_len, out) == 0;
#else
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info)
    {
        return false;
    }

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const int setup_result = mbedtls_md_setup(&ctx, md_info, 1);
    if (setup_result != 0)
    {
        mbedtls_md_free(&ctx);
        return false;
    }

    const int result = mbedtls_pkcs5_pbkdf2_hmac(&ctx, password, password_len, salt, salt_len, iterations, out_len, out);
    mbedtls_md_free(&ctx);
    return result == 0;
#endif
}

} // namespace Aegis
