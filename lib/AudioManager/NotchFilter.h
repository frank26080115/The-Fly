#pragma once

#include "AudioFilter.h"

#include <cstddef>
#include <cstdint>

class NotchFilter : public AudioFilter
{
public:
    explicit NotchFilter(float frequencyHz, float quality = 0.8f, uint32_t sampleRateHz = 16000);

    void process(int16_t* samples, size_t sampleCount) override;
    void reset();

private:
    void configure(float frequencyHz, float quality, uint32_t sampleRateHz);

    float b0_ = 1.0f;
    float b1_ = 0.0f;
    float b2_ = 0.0f;
    float a1_ = 0.0f;
    float a2_ = 0.0f;
    float z1_ = 0.0f;
    float z2_ = 0.0f;
};
