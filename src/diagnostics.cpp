// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "thefly_common.h"

#include "AudioFifo.h"
#include "ClockAgent.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>
#include <atomic>
#include <limits.h>

namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr uint32_t kReportIntervalMs = 1000;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

std::atomic<uint32_t> g_worst_free_stack        = {UINT32_MAX};
std::atomic<uint32_t> g_worst_free_internal_heap = {UINT32_MAX};
std::atomic<uint32_t> g_worst_free_psram_heap    = {UINT32_MAX};
std::atomic<bool>     g_memory_valid            = {false};
std::atomic<uint32_t> g_core0_ticks             = {0};
std::atomic<uint32_t> g_core1_ticks             = {0};
std::atomic<uint32_t> g_gui_draws               = {0};
std::atomic<uint32_t> g_long_writes             = {0};
std::atomic<uint32_t> g_i2s_input_samples       = {0};
std::atomic<uint32_t> g_i2s_output_samples      = {0};
std::atomic<uint32_t> g_last_report_ms          = {0};

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

void update_min(std::atomic<uint32_t>& target, uint32_t value);
void format_report_time(char* out, size_t out_size);

} // namespace

namespace Diagnostics
{

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void poll()
{
    memory_check_in();

    const uint32_t now            = millis();
    uint32_t       last_report_ms = g_last_report_ms.load(std::memory_order_relaxed);
    if (last_report_ms == 0)
    {
        g_last_report_ms.compare_exchange_strong(last_report_ms, now, std::memory_order_relaxed);
        return;
    }

    const uint32_t elapsed_ms = now - last_report_ms;
    if (elapsed_ms < kReportIntervalMs)
    {
        return;
    }
    if (!g_last_report_ms.compare_exchange_strong(last_report_ms, now, std::memory_order_relaxed))
    {
        return;
    }

    const MemoryStats           memory       = memory_stats();
    const uint32_t              core0_ticks  = g_core0_ticks.exchange(0, std::memory_order_relaxed);
    const uint32_t              core1_ticks  = g_core1_ticks.exchange(0, std::memory_order_relaxed);
    const uint32_t              gui_draws    = g_gui_draws.exchange(0, std::memory_order_relaxed);
    const uint32_t              long_writes  = g_long_writes.exchange(0, std::memory_order_relaxed);
    const uint32_t              i2s_in       = g_i2s_input_samples.exchange(0, std::memory_order_relaxed);
    const uint32_t              i2s_out      = g_i2s_output_samples.exchange(0, std::memory_order_relaxed);
    const AudioFifo::FlowEvents fifo_events  = AudioFifo::takeGlobalFlowEvents();
    const float                 scale        = 1000.0f / static_cast<float>(elapsed_ms);
    const float                 core0_rate   = static_cast<float>(core0_ticks) * scale;
    const float                 core1_rate   = static_cast<float>(core1_ticks) * scale;
    const float                 frame_rate   = static_cast<float>(gui_draws) * scale;
    const float                 long_rate    = static_cast<float>(long_writes) * scale;
    const float                 i2s_in_rate  = static_cast<float>(i2s_in) * scale;
    const float                 i2s_out_rate = static_cast<float>(i2s_out) * scale;
    const float                 over_rate    = static_cast<float>(fifo_events.overflow) * scale;
    const float                 under_rate   = static_cast<float>(fifo_events.underflow) * scale;
    char                        time_text[9] = {};

    format_report_time(time_text, sizeof(time_text));

#if defined(BUILD_IS_DEBUG) && defined(BUILD_PERIODIC_DIAGNOSTICS)
    Serial.printf("diag t=%s stk_min=%lu ih_min=%lu ps_min=%lu c0=%.1f c1=%.1f fps=%.1f "
                  "lw=%.1f i2si=%.1f i2so=%.1f fo=%.1f fu=%.1f\n",
                  time_text,
                  static_cast<unsigned long>(memory.worstFreeStack),
                  static_cast<unsigned long>(memory.worstFreeInternalHeap),
                  static_cast<unsigned long>(memory.worstFreePsramHeap),
                  core0_rate,
                  core1_rate,
                  frame_rate,
                  long_rate,
                  i2s_in_rate,
                  i2s_out_rate,
                  over_rate,
                  under_rate);
#endif
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

void core0Tick()
{
    memory_check_in();
    g_core0_ticks.fetch_add(1, std::memory_order_relaxed);
}

void core1Tick()
{
    memory_check_in();
    g_core1_ticks.fetch_add(1, std::memory_order_relaxed);
}

void memory_check_in()
{
    const uint32_t free_stack         = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
    const uint32_t free_internal_heap =
        static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const uint32_t free_psram_heap =
        static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    update_min(g_worst_free_stack, free_stack);
    update_min(g_worst_free_internal_heap, free_internal_heap);
    update_min(g_worst_free_psram_heap, free_psram_heap);
    g_memory_valid.store(true, std::memory_order_release);
}

MemoryStats memory_stats()
{
    MemoryStats stats;
    stats.valid                 = g_memory_valid.load(std::memory_order_acquire);
    stats.worstFreeStack        = stats.valid ? g_worst_free_stack.load(std::memory_order_relaxed) : 0;
    stats.worstFreeInternalHeap = stats.valid ? g_worst_free_internal_heap.load(std::memory_order_relaxed) : 0;
    stats.worstFreePsramHeap    = stats.valid ? g_worst_free_psram_heap.load(std::memory_order_relaxed) : 0;
    return stats;
}

// -----------------------------------------------------------------------------
// Event Counters
// -----------------------------------------------------------------------------

void gui_drew()
{
    g_gui_draws.fetch_add(1, std::memory_order_relaxed);
}

void long_write_exceeded()
{
    g_long_writes.fetch_add(1, std::memory_order_relaxed);
}

void i2s_input_samples(uint32_t samples)
{
    g_i2s_input_samples.fetch_add(samples, std::memory_order_relaxed);
}

void i2s_output_samples(uint32_t samples)
{
    g_i2s_output_samples.fetch_add(samples, std::memory_order_relaxed);
}

} // namespace Diagnostics

namespace
{

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

void update_min(std::atomic<uint32_t>& target, uint32_t value)
{
    uint32_t observed = target.load(std::memory_order_relaxed);
    while (value < observed &&
           !target.compare_exchange_weak(observed, value, std::memory_order_relaxed, std::memory_order_relaxed))
    {
    }
}

// -----------------------------------------------------------------------------
// Debug / Logging Helpers
// -----------------------------------------------------------------------------

void format_report_time(char* out, size_t out_size)
{
    m5::rtc_datetime_t now = {};
    if (!Clock.getDateTime(&now))
    {
        snprintf(out, out_size, "00:00:00");
        return;
    }

    snprintf(out,
             out_size,
             "%02u:%02u:%02u",
             static_cast<unsigned>(now.time.hours),
             static_cast<unsigned>(now.time.minutes),
             static_cast<unsigned>(now.time.seconds));
}

} // namespace
