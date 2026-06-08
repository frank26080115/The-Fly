/*
The waveform is unsteady, each period is different

Nfalling 8191369
Nrising  8191369
fmin     7692307.692303283 Hz
fmax     9090909.090913761 Hz
fmean    8191368.901050579 Hz

NOTE: even measured over 10 seconds with the Saleae Logic with a 100 mega-sample/s setting
the results between using LEDC and using the actual MCLK generator are identical
at least according to the measurement tool
*/

#include <Arduino.h>

#include "driver/ledc.h"
#include "esp_err.h"

#include "AudioManager.h"
#include "pins.h"

namespace
{

constexpr const char* TAG          = "test_ledcpwm";
constexpr uint32_t    kFrequencyHz = 8192000;

void report_result(const char* step, esp_err_t err)
{
    Serial.printf("%s: %s: %s\n", TAG, step, esp_err_to_name(err));
}

} // namespace

void test_ledcpwm()
{
    Serial.begin(115200, SERIAL_8N1, -1, 1);
    AudioManager::configure_i2s_shared();
    delay(250);

    ledc_timer_config_t timer = {};
    timer.speed_mode          = LEDC_HIGH_SPEED_MODE;
    timer.duty_resolution     = LEDC_TIMER_1_BIT;
    timer.timer_num           = LEDC_TIMER_0;
    timer.freq_hz             = kFrequencyHz;
    timer.clk_cfg             = LEDC_USE_APB_CLK;

    esp_err_t err = ledc_timer_config(&timer);
    report_result("timer", err);
    if (err == ESP_OK)
    {
        ledc_channel_config_t channel = {};
        channel.gpio_num              = kLedcMclkGpio;
        channel.speed_mode            = LEDC_HIGH_SPEED_MODE;
        channel.channel               = LEDC_CHANNEL_0;
        channel.intr_type             = LEDC_INTR_DISABLE;
        channel.timer_sel             = LEDC_TIMER_0;
        channel.duty                  = 1;
        channel.hpoint                = 0;

        err = ledc_channel_config(&channel);
        report_result("channel", err);
    }

    Serial.printf("%s: GPIO%d target=%lu Hz duty=50%% resolution=1-bit\n",
                  TAG,
                  kLedcMclkGpio,
                  static_cast<unsigned long>(kFrequencyHz));

    while (true)
    {
        delay(1000);
    }
}
