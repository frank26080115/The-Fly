#include <Arduino.h>
#include <string.h>

#include "Aegis.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/version.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG                      = "test_pbkdfbenchmark";
constexpr uint32_t    kFirstIterationCount     = 10;
constexpr uint32_t    kWatchdogServiceStartsAt = 256;
constexpr uint32_t    kRestartAfterTotalMs     = 2000;

const uint8_t kPassword[] = {
    0x2E, 0x92, 0x4A, 0x10, 0x54, 0xC1, 0x7B, 0x60, 0xAD, 0x42, 0x16, 0xEE, 0x71, 0x33, 0x8E, 0x19,
    0xC8, 0xA4, 0xE0, 0x66, 0x53, 0x99, 0x20, 0xFD, 0x86, 0x31, 0x4F, 0xAA, 0x0D, 0x5B, 0x37, 0xC2,
};

const uint8_t kSalt[] = {
    0x91, 0xE3, 0x5C, 0xF0, 0x12, 0x74, 0xA9, 0x8B, 0x43, 0xD6, 0x0E, 0xAF, 0x28, 0x6D, 0xB1, 0x7C,
    0x5A, 0x09, 0xEE, 0x3F, 0xB8, 0x61, 0x14, 0xC5, 0x72, 0x9A, 0x01, 0xD4, 0xEF, 0x38, 0xBC, 0x27,
};

uint32_t next_iteration_count(uint32_t rounds)
{
    const uint32_t next = (rounds * 6U + 4U) / 5U;
    return next > rounds ? next : rounds + 1U;
}

bool pbkdf2_without_watchdog_service(uint32_t iterations, uint8_t out[Aegis::kSha256Size])
{
#if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER >= 0x03000000
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
                                         kPassword,
                                         sizeof(kPassword),
                                         kSalt,
                                         sizeof(kSalt),
                                         iterations,
                                         Aegis::kSha256Size,
                                         out) == 0;
#else
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info)
    {
        return false;
    }

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, md_info, 1) != 0)
    {
        mbedtls_md_free(&ctx);
        return false;
    }

    const int result = mbedtls_pkcs5_pbkdf2_hmac(&ctx,
                                                 kPassword,
                                                 sizeof(kPassword),
                                                 kSalt,
                                                 sizeof(kSalt),
                                                 iterations,
                                                 Aegis::kSha256Size,
                                                 out);
    mbedtls_md_free(&ctx);
    return result == 0;
#endif
}

bool run_pbkdf_case(uint32_t iterations, bool service_watchdog, uint32_t& elapsed_ms)
{
    uint8_t        out[Aegis::kSha256Size] = {};
    const uint32_t started                 = millis();
    const bool     ok =
        service_watchdog
            ? Aegis::pbkdf2HmacSha256(kPassword, sizeof(kPassword), kSalt, sizeof(kSalt), iterations, out, sizeof(out))
            : pbkdf2_without_watchdog_service(iterations, out);
    elapsed_ms = millis() - started;

    Serial.printf("%s: rounds=%lu elapsed=%lu ms watchdog_service=%s ok=%s digest0=%02X\n",
                  TAG,
                  static_cast<unsigned long>(iterations),
                  static_cast<unsigned long>(elapsed_ms),
                  service_watchdog ? "yes" : "no",
                  ok ? "yes" : "no",
                  static_cast<unsigned>(out[0]));

    mbedtls_platform_zeroize(out, sizeof(out));
    return ok;
}

void run_benchmark_phase(bool service_watchdog, uint32_t stop_after_total_ms)
{
    uint32_t iterations = kFirstIterationCount;
    uint32_t total_ms   = 0;

    Serial.printf("%s: phase begin watchdog_service=%s\n", TAG, service_watchdog ? "yes" : "no");
    while (true)
    {
        const bool servicing_now = service_watchdog && iterations >= kWatchdogServiceStartsAt;
        uint32_t   elapsed_ms    = 0;
        if (!run_pbkdf_case(iterations, servicing_now, elapsed_ms))
        {
            Serial.printf("%s: PBKDF2 failed at rounds=%lu\n", TAG, static_cast<unsigned long>(iterations));
            break;
        }

        total_ms += elapsed_ms;
        Serial.printf("%s: total=%lu ms\n", TAG, static_cast<unsigned long>(total_ms));
        Serial.flush();

        if (stop_after_total_ms > 0 && total_ms >= stop_after_total_ms)
        {
            Serial.printf("%s: phase reached %lu ms total\n", TAG, static_cast<unsigned long>(total_ms));
            break;
        }

        iterations = next_iteration_count(iterations);
    }
}

} // namespace

void test_pbkdfbenchmark()
{
    Serial.begin(115200);
    delay(3000);

    Serial.println();
    Serial.printf("%s: starting PBKDF2 benchmark\n", TAG);
    Serial.printf("%s: first phase starts at %lu rounds, grows by 20 percent, services watchdog from %lu rounds\n",
                  TAG,
                  static_cast<unsigned long>(kFirstIterationCount),
                  static_cast<unsigned long>(kWatchdogServiceStartsAt));

    run_benchmark_phase(true, kRestartAfterTotalMs);

    Serial.printf("%s: restarting benchmark without watchdog service; WDT reset is expected\n", TAG);
    Serial.flush();
    delay(1000);
    run_benchmark_phase(false, 0);

    idle_forever();
}
