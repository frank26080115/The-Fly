#include "PinCode.h"

#include <mutex>
#include <string.h>

#include "Aegis.h"
#include "ClockAgent.h"
#include "dbg_log.h"
#include "esp_err.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"

namespace PinCode
{
namespace
{

constexpr const char* TAG                 = "PinCode";
constexpr const char* kNvsNamespace       = "aegis";
constexpr const char* kPinCodeCfgBlobName = "pin_code_cfg";

pin_code_cfg_t g_cfg         = {};
bool           g_initialized = false;
std::mutex     g_mutex;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static bool      ensure_initialized_locked();
static bool      load_or_create_cfg_locked();
static bool      save_cfg_locked();
static esp_err_t save_cfg_to_handle_locked(nvs_handle_t handle);
static void      revoke_pin_locked();
static void      sanitize_cfg_locked();
static void      clear_cfg_locked();
static void      store_u32_be(uint8_t* dst, uint32_t value);
static time_t    current_time();

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

bool init()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return ensure_initialized_locked();
}

bool logBadAttempt(uint32_t* failedAttempts)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensure_initialized_locked())
    {
        return false;
    }

    if (g_cfg.bad_attempts < UINT32_MAX)
    {
        ++g_cfg.bad_attempts;
    }

    if (failedAttempts)
    {
        *failedAttempts = g_cfg.bad_attempts;
    }

    if (g_cfg.bad_attempts > PIN_CODE_MAX_BAD_ATTEMPTS_ALLOWED)
    {
        revoke_pin_locked();
    }

    return save_cfg_locked();
}

bool logSuccessfulUsage()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensure_initialized_locked())
    {
        return false;
    }

    g_cfg.bad_attempts = 0;
    return save_cfg_locked();
}

bool revokePin()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensure_initialized_locked())
    {
        return false;
    }

    revoke_pin_locked();
    return save_cfg_locked();
}

#if BUILD_WITH_SECURITY_LEVEL == 1
bool setPin(const char* pin)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensure_initialized_locked())
    {
        return false;
    }

    memset(g_cfg.pin_code, 0, sizeof(g_cfg.pin_code));
    if (pin)
    {
        strncpy(g_cfg.pin_code, pin, sizeof(g_cfg.pin_code) - 1);
        g_cfg.pin_code[sizeof(g_cfg.pin_code) - 1] = '\0';
    }

    g_cfg.active       = true;
    g_cfg.bad_attempts = 0;
    g_cfg.salt         = 0;
    g_cfg.date_set     = current_time();
    g_cfg.date_revoked = 0;
    return save_cfg_locked();
}
#endif

bool regeneratePin()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensure_initialized_locked())
    {
        return false;
    }

    const uint8_t* filecrypt_key = Aegis::getFilecryptKey();
    if (!filecrypt_key)
    {
        DBG_LOGE(TAG, "cannot regenerate PIN without filecrypt key");
        return false;
    }

    const uint32_t next_salt                     = g_cfg.salt + 1;
    uint8_t        salt_bytes[sizeof(next_salt)] = {};
    uint8_t        digest[Aegis::kSha256Size]    = {};
    store_u32_be(salt_bytes, next_salt);

    if (!Aegis::hmacSha256(filecrypt_key, Aegis::kFilecryptKeySize, salt_bytes, sizeof(salt_bytes), digest))
    {
        mbedtls_platform_zeroize(digest, sizeof(digest));
        DBG_LOGE(TAG, "PIN regeneration hash failed");
        return false;
    }

    memset(g_cfg.pin_code, 0, sizeof(g_cfg.pin_code));
    const size_t pin_digits =
        PINCODE_DEFAULT_LENGTH < sizeof(g_cfg.pin_code) ? PINCODE_DEFAULT_LENGTH : sizeof(g_cfg.pin_code) - 1;
    for (size_t i = 0; i < pin_digits; ++i)
    {
        g_cfg.pin_code[i] = static_cast<char>('1' + (digest[i] % 9));
    }
    g_cfg.pin_code[pin_digits] = '\0';

    g_cfg.active       = true;
    g_cfg.bad_attempts = 0;
    g_cfg.salt         = next_salt;
    g_cfg.date_set     = current_time();
    g_cfg.date_revoked = 0;
    mbedtls_platform_zeroize(digest, sizeof(digest));
    return save_cfg_locked();
}

const char* getPin()
{
#ifdef TEST_MOCK_PIN_CODE
    return "123456";
#else
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensure_initialized_locked() || !g_cfg.active)
    {
        return nullptr;
    }
    sanitize_cfg_locked();
    return g_cfg.pin_code;
#endif
}

bool getConfig(pin_code_cfg_t& out)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensure_initialized_locked())
    {
        memset(&out, 0, sizeof(out));
        return false;
    }

    sanitize_cfg_locked();
    out = g_cfg;
    return true;
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

bool ensure_initialized_locked()
{
    if (g_initialized)
    {
        return true;
    }
    return load_or_create_cfg_locked();
}

bool load_or_create_cfg_locked()
{
    nvs_handle_t handle = 0;
    esp_err_t    err    = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        DBG_LOGE(TAG, "NVS open for PIN load failed: %s", esp_err_to_name(err));
        clear_cfg_locked();
        return false;
    }

    pin_code_cfg_t loaded    = {};
    size_t         read_size = sizeof(loaded);
    err                      = nvs_get_blob(handle, kPinCodeCfgBlobName, &loaded, &read_size);
    if (err == ESP_OK && read_size == sizeof(loaded))
    {
        g_cfg = loaded;
        sanitize_cfg_locked();
        nvs_close(handle);
        g_initialized = true;
        return true;
    }

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        DBG_LOGI(TAG, "NVS PIN config not found; creating empty config");
    }
    else
    {
        DBG_LOGW(TAG,
                 "ignoring incompatible PIN config in NVS: err=%s size=%u expected=%u",
                 esp_err_to_name(err),
                 static_cast<unsigned>(read_size),
                 static_cast<unsigned>(sizeof(loaded)));
    }

    clear_cfg_locked();
    err = save_cfg_to_handle_locked(handle);
    nvs_close(handle);
    if (err != ESP_OK)
    {
        DBG_LOGE(TAG, "NVS empty PIN config save failed: %s", esp_err_to_name(err));
        return false;
    }

    g_initialized = true;
    return true;
}

bool save_cfg_locked()
{
    nvs_handle_t handle = 0;
    esp_err_t    err    = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        DBG_LOGE(TAG, "NVS open for PIN save failed: %s", esp_err_to_name(err));
        return false;
    }

    err = save_cfg_to_handle_locked(handle);
    nvs_close(handle);

    if (err != ESP_OK)
    {
        DBG_LOGE(TAG, "NVS PIN save failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

esp_err_t save_cfg_to_handle_locked(nvs_handle_t handle)
{
    esp_err_t err = nvs_set_blob(handle, kPinCodeCfgBlobName, &g_cfg, sizeof(g_cfg));
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    return err;
}

void revoke_pin_locked()
{
    memset(g_cfg.pin_code, 0, sizeof(g_cfg.pin_code));
    g_cfg.active       = false;
    g_cfg.date_revoked = current_time();
}

void sanitize_cfg_locked()
{
    g_cfg.pin_code[sizeof(g_cfg.pin_code) - 1] = '\0';
}

void clear_cfg_locked()
{
    memset(&g_cfg, 0, sizeof(g_cfg));
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

void store_u32_be(uint8_t* dst, uint32_t value)
{
    dst[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    dst[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dst[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dst[3] = static_cast<uint8_t>(value & 0xFF);
}

time_t current_time()
{
    return Clock.getUnixTime();
}

} // namespace

} // namespace PinCode
