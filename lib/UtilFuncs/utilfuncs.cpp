#include "utilfuncs.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

namespace
{
constexpr const char *TAG = "utilfuncs";
}

bool ok(esp_err_t err, const char *what)
{
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE)
    {
        return true;
    }
    ESP_LOGE(TAG, "%s failed: %s", what, esp_err_to_name(err));
    return false;
}

void copy_bda(uint8_t dst[ESP_BD_ADDR_LEN], const uint8_t src[ESP_BD_ADDR_LEN])
{
    memcpy(dst, src, ESP_BD_ADDR_LEN);
}

void log_bda(const char *label, const uint8_t bda[ESP_BD_ADDR_LEN])
{
    ESP_LOGI(TAG, "%s %02x:%02x:%02x:%02x:%02x:%02x",
             label, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

bool bda_equal(const uint8_t a[ESP_BD_ADDR_LEN], const uint8_t b[ESP_BD_ADDR_LEN])
{
    return memcmp(a, b, ESP_BD_ADDR_LEN) == 0;
}

bool parse_hex_byte(const char *text, uint8_t &value)
{
    char *end = nullptr;
    const unsigned long parsed = strtoul(text, &end, 16);
    if (end != text + 2 || parsed > 0xff)
    {
        return false;
    }
    value = static_cast<uint8_t>(parsed);
    return true;
}

bool parse_mac(const char *mac, esp_bd_addr_t out)
{
    if (!mac || strlen(mac) != 17)
    {
        return false;
    }

    for (int i = 0; i < ESP_BD_ADDR_LEN; ++i)
    {
        if (!isxdigit(static_cast<unsigned char>(mac[i * 3])) ||
            !isxdigit(static_cast<unsigned char>(mac[i * 3 + 1])) ||
            (i < ESP_BD_ADDR_LEN - 1 && mac[i * 3 + 2] != ':'))
        {
            return false;
        }
        if (!parse_hex_byte(mac + (i * 3), out[i]))
        {
            return false;
        }
    }

    return true;
}

size_t upsample_s16_mono_2x_duplicate(const int16_t *src, size_t src_samples, int16_t *dst)
{
    if (!src || !dst)
    {
        return 0;
    }

    for (size_t i = 0; i < src_samples; ++i)
    {
        const int16_t sample = src[i];
        dst[(i << 1)] = sample;
        dst[(i << 1) + 1] = sample;
    }

    return src_samples << 1;
}

size_t upsample_s16_mono_2x_linear(const int16_t *src, size_t src_samples, int16_t *dst, Upsample2xLinearState &state)
{
    if (!src || !dst)
    {
        return 0;
    }

    size_t out = 0;
    int32_t prev = state.prev;
    bool has_prev = state.has_prev;

    for (size_t i = 0; i < src_samples; ++i)
    {
        const int32_t sample = src[i];
        dst[out++] = has_prev ? static_cast<int16_t>((prev + sample) >> 1) : static_cast<int16_t>(sample);
        dst[out++] = static_cast<int16_t>(sample);
        prev = sample;
        has_prev = true;
    }

    state.prev = static_cast<int16_t>(prev);
    state.has_prev = has_prev;
    return out;
}

void reset_upsample_2x_linear_state(Upsample2xLinearState &state)
{
    state.has_prev = false;
    state.prev = 0;
}
