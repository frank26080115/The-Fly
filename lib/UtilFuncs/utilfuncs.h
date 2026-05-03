#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_gap_bt_api.h"

/*
use this library for utility functions more complex than just inline functions or macros
*/

bool ok(esp_err_t err, const char* what);
void copy_bda(uint8_t dst[ESP_BD_ADDR_LEN], const uint8_t src[ESP_BD_ADDR_LEN]);
void log_bda(const char* label, const uint8_t bda[ESP_BD_ADDR_LEN]);
bool bda_equal(const uint8_t a[ESP_BD_ADDR_LEN], const uint8_t b[ESP_BD_ADDR_LEN]);
bool parse_hex_byte(const char* text, uint8_t& value);
bool parse_mac(const char* mac, esp_bd_addr_t out);

struct Upsample2xLinearState
{
    bool    has_prev = false;
    int16_t prev     = 0;
};

size_t upsample_s16_mono_2x_duplicate(const int16_t* src, size_t src_samples, int16_t* dst);
size_t upsample_s16_mono_2x_linear(const int16_t* src, size_t src_samples, int16_t* dst, Upsample2xLinearState& state);
void   reset_upsample_2x_linear_state(Upsample2xLinearState& state);
size_t mono_s16_to_stereo_s16(const int16_t* src, size_t src_samples, int16_t* dst);
