#pragma once

#include <cstddef>
#include <cstdint>

extern "C"
{
#include "shine/src/lib/layer3.h"
}

typedef void (*MP3CallbackFDK)(uint8_t* mp3_data, size_t len);

class ShineWrapper
{
public:
    ShineWrapper(uint32_t sample_rate,
                 int num_of_channels,
                 int bits_per_channel,
                 int quality,
                 int bit_rate_kbps,
                 bool disable_bit_reservoir,
                 MP3CallbackFDK callback);
    ~ShineWrapper();

    ShineWrapper(const ShineWrapper&)            = delete;
    ShineWrapper& operator=(const ShineWrapper&) = delete;

    bool    begin();
    void    end();
    int     getSamplesPerPass() const;
    int32_t write(void* pcm_samples, int bytes);
    bool    flush();

    void setDataCallback(MP3CallbackFDK callback);
    explicit operator bool() const;

private:
    bool encode_pass(int16_t* interleaved_samples);
    void provide_result(unsigned char* data, int len);

    shine_config_t config_ = {};
    shine_t        shine_  = nullptr;
    MP3CallbackFDK callback_ = nullptr;

    uint32_t sample_rate_           = 0;
    int      num_of_channels_       = 0;
    int      bits_per_channel_      = 0;
    int      quality_               = 0;
    int      bit_rate_kbps_         = 0;
    bool     disable_bit_reservoir_ = true;
    bool     active_                = false;

    int      samples_per_pass_      = 0;
    size_t   input_buffer_samples_  = 0;
    size_t   input_buffer_bytes_    = 0;
    size_t   buffered_bytes_        = 0;
    int16_t* input_buffer_          = nullptr;
};
