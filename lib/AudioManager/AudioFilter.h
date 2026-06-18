#pragma once

#include <cstddef>
#include <cstdint>

class AudioFilter
{
public:
    virtual void process(int16_t* samples, size_t sampleCount) = 0;
};
