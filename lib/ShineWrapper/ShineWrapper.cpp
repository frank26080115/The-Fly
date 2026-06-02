/*
This is a C++ wrapper for the `shine` MP3 encoder library
The wrapper attempts to mimic another library `arduino-liblame` in the way it works
The most important point is how this wrapper has its own staging buffer
*/

#include "ShineWrapper.h"

#include <cstring>
#include <new>

ShineWrapper::ShineWrapper(uint32_t       sample_rate,
                           int            num_of_channels,
                           int            bits_per_channel,
                           int            quality,
                           int            bit_rate_kbps,
                           bool           disable_bit_reservoir,
                           MP3CallbackFDK callback)
    : callback_(callback), sample_rate_(sample_rate), num_of_channels_(num_of_channels),
      bits_per_channel_(bits_per_channel), quality_(quality), bit_rate_kbps_(bit_rate_kbps),
      disable_bit_reservoir_(disable_bit_reservoir)
{
    std::memset(&config_, 0, sizeof(config_));
    shine_set_config_mpeg_defaults(&config_.mpeg);

    config_.wave.samplerate = static_cast<int>(sample_rate_);
    config_.wave.channels   = num_of_channels_ == 1 ? PCM_MONO : PCM_STEREO;
    config_.mpeg.mode       = num_of_channels_ == 1 ? MONO : STEREO;
    config_.mpeg.bitr       = bit_rate_kbps_;

    // NOTE: disable_bit_reservoir has no effect here
    // but, the vendored Shine encoder appears to have the reservoir effectively disabled already:
    // layer3.c (line 108) initializes ResvMax = 0.
    // reservoir.c (line 30) immediately returns normal per-frame bits when ResvMax is zero.
    // l3bitstream.c (line 103) writes main_data_begin as zero for both MPEG-I and MPEG-II paths.
}

ShineWrapper::~ShineWrapper()
{
    end();
}

bool ShineWrapper::begin()
{
    end();

    if ((num_of_channels_ != 1 && num_of_channels_ != 2) || bits_per_channel_ != 16)
    {
        return false;
    }

    if (shine_check_config(config_.wave.samplerate, config_.mpeg.bitr) < 0)
    {
        return false;
    }

    shine_ = shine_initialise(&config_);
    if (!shine_)
    {
        return false;
    }

    samples_per_pass_ = shine_samples_per_pass(shine_);
    if (samples_per_pass_ <= 0)
    {
        end();
        return false;
    }

    input_buffer_samples_ = static_cast<size_t>(samples_per_pass_) * static_cast<size_t>(num_of_channels_);
    input_buffer_bytes_   = input_buffer_samples_ * sizeof(int16_t);
    buffered_bytes_       = 0;

    input_buffer_ = new (std::nothrow) int16_t[input_buffer_samples_];
    if (!input_buffer_)
    {
        end();
        return false;
    }

    active_ = true;
    return true;
}

void ShineWrapper::end()
{
    active_               = false;
    buffered_bytes_       = 0;
    samples_per_pass_     = 0;
    input_buffer_samples_ = 0;
    input_buffer_bytes_   = 0;

    if (shine_)
    {
        shine_close(shine_);
        shine_ = nullptr;
    }

    delete[] input_buffer_;
    input_buffer_ = nullptr;
}

int ShineWrapper::getSamplesPerPass() const
{
    return shine_ ? shine_samples_per_pass(shine_) : 0;
}

int32_t ShineWrapper::write(void* pcm_samples, int bytes)
{
    if (!active_ || !shine_ || !pcm_samples || bytes <= 0 || !input_buffer_ || input_buffer_bytes_ == 0)
    {
        return 0;
    }

    const int frame_bytes = num_of_channels_ * static_cast<int>(sizeof(int16_t));
    if (frame_bytes <= 0 || bytes % frame_bytes != 0)
    {
        return 0;
    }

    uint8_t* src       = static_cast<uint8_t*>(pcm_samples);
    int      remaining = bytes;
    int32_t  consumed  = 0;

    while (remaining > 0)
    {
        if (buffered_bytes_ == 0 && static_cast<size_t>(remaining) >= input_buffer_bytes_)
        {
            if (!encode_pass(reinterpret_cast<int16_t*>(src)))
            {
                return consumed;
            }
            src += input_buffer_bytes_;
            remaining -= static_cast<int>(input_buffer_bytes_);
            consumed += static_cast<int32_t>(input_buffer_bytes_);
            continue;
        }

        const size_t space      = input_buffer_bytes_ - buffered_bytes_;
        const size_t copy_bytes = static_cast<size_t>(remaining) < space ? static_cast<size_t>(remaining) : space;

        std::memcpy(reinterpret_cast<uint8_t*>(input_buffer_) + buffered_bytes_, src, copy_bytes);
        buffered_bytes_ += copy_bytes;
        src += copy_bytes;
        remaining -= static_cast<int>(copy_bytes);
        consumed += static_cast<int32_t>(copy_bytes);

        if (buffered_bytes_ == input_buffer_bytes_)
        {
            if (!encode_pass(input_buffer_))
            {
                buffered_bytes_ = 0;
                return consumed - static_cast<int32_t>(copy_bytes);
            }
            buffered_bytes_ = 0;
        }
    }

    return consumed;
}

bool ShineWrapper::flush()
{
    if (!shine_)
    {
        return true;
    }

    if (active_ && buffered_bytes_ > 0)
    {
        std::memset(reinterpret_cast<uint8_t*>(input_buffer_) + buffered_bytes_,
                    0,
                    input_buffer_bytes_ - buffered_bytes_);
        if (!encode_pass(input_buffer_))
        {
            return false;
        }
        buffered_bytes_ = 0;
    }

    int            written = 0;
    unsigned char* data    = shine_flush(shine_, &written);
    provide_result(data, written);
    active_ = false;
    return written >= 0;
}

void ShineWrapper::setDataCallback(MP3CallbackFDK callback)
{
    callback_ = callback;
}

ShineWrapper::operator bool() const
{
    return active_;
}

bool ShineWrapper::encode_pass(int16_t* interleaved_samples)
{
    if (!shine_ || !interleaved_samples)
    {
        return false;
    }

    int            written = 0;
    unsigned char* data    = shine_encode_buffer_interleaved(shine_, interleaved_samples, &written);
    if (written < 0)
    {
        return false;
    }

    provide_result(data, written);
    return true;
}

void ShineWrapper::provide_result(unsigned char* data, int len)
{
    if (callback_ && data && len > 0)
    {
        callback_(reinterpret_cast<uint8_t*>(data), static_cast<size_t>(len));
    }
}
