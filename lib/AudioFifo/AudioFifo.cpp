#include "AudioFifo.h"

std::atomic<uint32_t> AudioFifo::s_globalOverflowEvents_  = {0};
std::atomic<uint32_t> AudioFifo::s_globalUnderflowEvents_ = {0};
