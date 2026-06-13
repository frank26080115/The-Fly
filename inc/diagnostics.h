#pragma once

#include <stdint.h>

namespace Diagnostics
{

struct MemoryStats
{
    bool     valid                 = false;
    uint32_t worstFreeStack        = 0;
    uint32_t worstFreeInternalHeap = 0;
    uint32_t worstFreePsramHeap    = 0;
};

void        memory_check_in();
MemoryStats memory_stats();
void        core0Tick();
void        core1Tick();
void        gui_drew();
void        long_write_exceeded();
void        i2s_input_samples(uint32_t samples);
void        i2s_output_samples(uint32_t samples);
void        poll();

} // namespace Diagnostics
