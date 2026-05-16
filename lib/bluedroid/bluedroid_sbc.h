#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "decoder/include/oi_codec_sbc.h"
#include "encoder/include/sbc_encoder.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define BLUEDROID_SBC_FREQ_16000 0x00
#define BLUEDROID_SBC_FREQ_32000 0x01
#define BLUEDROID_SBC_FREQ_44100 0x02
#define BLUEDROID_SBC_FREQ_48000 0x03

#define BLUEDROID_SBC_BLK_4 0x00
#define BLUEDROID_SBC_BLK_8 0x01
#define BLUEDROID_SBC_BLK_12 0x02
#define BLUEDROID_SBC_BLK_16 0x03
#define BLUEDROID_SBC_BLK_15 0x04

#define BLUEDROID_SBC_MODE_MONO 0x00
#define BLUEDROID_SBC_MODE_DUAL_CHANNEL 0x01
#define BLUEDROID_SBC_MODE_STEREO 0x02
#define BLUEDROID_SBC_MODE_JOINT_STEREO 0x03

#define BLUEDROID_SBC_AM_LOUDNESS 0x00
#define BLUEDROID_SBC_AM_SNR 0x01

#define BLUEDROID_SBC_SB_4 0x00
#define BLUEDROID_SBC_SB_8 0x01

#define BLUEDROID_SBC_LE 0x00

#define BLUEDROID_MSBC_FRAME_LENGTH 57
#define BLUEDROID_MSBC_CODESIZE 240

typedef struct bluedroid_sbc_struct
{
    unsigned long flags;

    uint8_t frequency;
    uint8_t blocks;
    uint8_t subbands;
    uint8_t mode;
    uint8_t allocation;
    uint8_t bitpool;
    uint8_t endian;

    OI_CODEC_SBC_DECODER_CONTEXT    decoder;
    OI_CODEC_SBC_CODEC_DATA_MONO    decoder_data_mono;
    OI_CODEC_SBC_CODEC_DATA_STEREO  decoder_data_stereo;
    SBC_ENC_PARAMS                  encoder;
    uint8_t                         decoder_ready;
    uint8_t                         encoder_ready;
} bluedroid_sbc_t;

int bluedroid_sbc_init(bluedroid_sbc_t* sbc, unsigned long flags);
int bluedroid_sbc_reinit(bluedroid_sbc_t* sbc, unsigned long flags);
int bluedroid_sbc_configure_msbc(bluedroid_sbc_t* sbc);

ssize_t bluedroid_sbc_parse(bluedroid_sbc_t* sbc, const void* input, size_t input_len);
ssize_t bluedroid_sbc_decode(bluedroid_sbc_t* sbc, const void* input, size_t input_len, void* output, size_t output_len, size_t* written);
ssize_t bluedroid_sbc_encode(bluedroid_sbc_t* sbc, const void* input, size_t input_len, void* output, size_t output_len, ssize_t* written);

size_t bluedroid_sbc_get_frame_length(bluedroid_sbc_t* sbc);
unsigned bluedroid_sbc_get_frame_duration(bluedroid_sbc_t* sbc);
size_t bluedroid_sbc_get_codesize(bluedroid_sbc_t* sbc);

const char* bluedroid_sbc_get_implementation_info(bluedroid_sbc_t* sbc);
void        bluedroid_sbc_finish(bluedroid_sbc_t* sbc);

#ifdef __cplusplus
}
#endif
