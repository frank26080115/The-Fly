/*
test failed
oscilloscope shows a 10 MHz waveform, not 8.192 MHz
*/

#include <Arduino.h>

#include "driver/mcpwm_prelude.h"
#include "esp_err.h"

namespace
{

constexpr const char* TAG                = "test_mcpwm";
constexpr int         kOutputGpio        = 14;
constexpr uint32_t    kFrequencyHz       = 8192000;
constexpr uint32_t    kPeriodTicks       = 2;
constexpr uint32_t    kCompareTicks      = 1;
constexpr uint32_t    kTimerResolutionHz = kFrequencyHz * kPeriodTicks;

bool report_result(const char* step, esp_err_t err)
{
    Serial.printf("%s: %s: %s\n", TAG, step, esp_err_to_name(err));
    return err == ESP_OK;
}

void idle_forever()
{
    while (true)
    {
        delay(1000);
    }
}

} // namespace

void test_mcpwm()
{
    Serial.begin(115200, SERIAL_8N1, -1, 1);
    delay(250);

    mcpwm_timer_handle_t timer = nullptr;
    mcpwm_timer_config_t timer_config = {};
    timer_config.group_id            = 0;
    timer_config.clk_src             = MCPWM_TIMER_CLK_SRC_DEFAULT;
    timer_config.resolution_hz       = kTimerResolutionHz;
    timer_config.period_ticks        = kPeriodTicks;
    timer_config.count_mode          = MCPWM_TIMER_COUNT_MODE_UP;

    if (!report_result("timer", mcpwm_new_timer(&timer_config, &timer)))
    {
        idle_forever();
    }

    mcpwm_oper_handle_t oper = nullptr;
    mcpwm_operator_config_t operator_config = {};
    operator_config.group_id                = 0;

    if (!report_result("operator", mcpwm_new_operator(&operator_config, &oper)))
    {
        idle_forever();
    }

    if (!report_result("connect timer", mcpwm_operator_connect_timer(oper, timer)))
    {
        idle_forever();
    }

    mcpwm_cmpr_handle_t comparator = nullptr;
    mcpwm_comparator_config_t comparator_config = {};
    comparator_config.flags.update_cmp_on_tez   = true;

    if (!report_result("comparator", mcpwm_new_comparator(oper, &comparator_config, &comparator)))
    {
        idle_forever();
    }

    if (!report_result("compare value", mcpwm_comparator_set_compare_value(comparator, kCompareTicks)))
    {
        idle_forever();
    }

    mcpwm_gen_handle_t generator = nullptr;
    mcpwm_generator_config_t generator_config = {};
    generator_config.gen_gpio_num             = kOutputGpio;

    if (!report_result("generator", mcpwm_new_generator(oper, &generator_config, &generator)))
    {
        idle_forever();
    }

    if (!report_result("timer action",
                       mcpwm_generator_set_action_on_timer_event(
                           generator,
                           MCPWM_GEN_TIMER_EVENT_ACTION(
                               MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH))))
    {
        idle_forever();
    }

    if (!report_result("compare action",
                       mcpwm_generator_set_action_on_compare_event(
                           generator,
                           MCPWM_GEN_COMPARE_EVENT_ACTION(
                               MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_LOW))))
    {
        idle_forever();
    }

    if (!report_result("enable timer", mcpwm_timer_enable(timer)))
    {
        idle_forever();
    }

    if (!report_result("start timer", mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP)))
    {
        idle_forever();
    }

    Serial.printf("%s: GPIO%d target=%lu Hz duty=50%% period=%lu ticks compare=%lu timer_resolution=%lu Hz\n",
                  TAG,
                  kOutputGpio,
                  static_cast<unsigned long>(kFrequencyHz),
                  static_cast<unsigned long>(kPeriodTicks),
                  static_cast<unsigned long>(kCompareTicks),
                  static_cast<unsigned long>(kTimerResolutionHz));

    idle_forever();
}
