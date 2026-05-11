#include "bluedroid_sbc.h"

#include <string.h>

#define BLUEDROID_SBC_OK 0
#define BLUEDROID_SBC_ERR (-1)

static int is_msbc(const bluedroid_sbc_t* sbc)
{
    return sbc && sbc->frequency == BLUEDROID_SBC_FREQ_16000 && sbc->mode == BLUEDROID_SBC_MODE_MONO && sbc->subbands == BLUEDROID_SBC_SB_8 && sbc->blocks == BLUEDROID_SBC_BLK_15;
}

static uint8_t channel_count(const bluedroid_sbc_t* sbc)
{
    return sbc && sbc->mode == BLUEDROID_SBC_MODE_MONO ? 1 : 2;
}

static uint8_t block_count(const bluedroid_sbc_t* sbc)
{
    if (!sbc)
    {
        return 0;
    }

    switch (sbc->blocks)
    {
    case BLUEDROID_SBC_BLK_4:
        return 4;
    case BLUEDROID_SBC_BLK_8:
        return 8;
    case BLUEDROID_SBC_BLK_12:
        return 12;
    case BLUEDROID_SBC_BLK_15:
        return 15;
    case BLUEDROID_SBC_BLK_16:
        return 16;
    default:
        return 0;
    }
}

static uint8_t oi_blocks(const bluedroid_sbc_t* sbc)
{
    switch (block_count(sbc))
    {
    case 4:
        return SBC_BLOCKS_4;
    case 8:
        return SBC_BLOCKS_8;
    case 12:
        return SBC_BLOCKS_12;
    default:
        return SBC_BLOCKS_16;
    }
}

static uint8_t subband_count(const bluedroid_sbc_t* sbc)
{
    return sbc && sbc->subbands == BLUEDROID_SBC_SB_4 ? 4 : 8;
}

static uint8_t oi_subbands(const bluedroid_sbc_t* sbc)
{
    return sbc && sbc->subbands == BLUEDROID_SBC_SB_4 ? SBC_SUBBANDS_4 : SBC_SUBBANDS_8;
}

static uint32_t sample_rate_hz(const bluedroid_sbc_t* sbc)
{
    if (!sbc)
    {
        return 0;
    }

    switch (sbc->frequency)
    {
    case BLUEDROID_SBC_FREQ_16000:
        return 16000;
    case BLUEDROID_SBC_FREQ_32000:
        return 32000;
    case BLUEDROID_SBC_FREQ_44100:
        return 44100;
    case BLUEDROID_SBC_FREQ_48000:
        return 48000;
    default:
        return 0;
    }
}

static int prepare_decoder(bluedroid_sbc_t* sbc)
{
    if (!sbc)
    {
        return BLUEDROID_SBC_ERR;
    }

    if (sbc->decoder_ready)
    {
        return BLUEDROID_SBC_OK;
    }

    OI_STATUS status;
    if (is_msbc(sbc))
    {
        status = OI_CODEC_mSBC_DecoderReset(&sbc->decoder, sbc->decoder_data_mono.data, sizeof(sbc->decoder_data_mono.data));
    }
    else
    {
        if (block_count(sbc) == 0 || sample_rate_hz(sbc) == 0)
        {
            return BLUEDROID_SBC_ERR;
        }

        if (channel_count(sbc) == 1)
        {
            status = OI_CODEC_SBC_DecoderReset(&sbc->decoder, sbc->decoder_data_mono.data, sizeof(sbc->decoder_data_mono.data), 1, 1, FALSE);
        }
        else
        {
            status = OI_CODEC_SBC_DecoderReset(&sbc->decoder, sbc->decoder_data_stereo.data, sizeof(sbc->decoder_data_stereo.data), 2, 2, FALSE);
        }

        if (!OI_SUCCESS(status))
        {
            return BLUEDROID_SBC_ERR;
        }

        status = OI_CODEC_SBC_DecoderConfigureRaw(&sbc->decoder, FALSE, sbc->frequency, sbc->mode, oi_subbands(sbc), oi_blocks(sbc), sbc->allocation, sbc->bitpool);
    }

    if (!OI_SUCCESS(status))
    {
        return BLUEDROID_SBC_ERR;
    }

    sbc->decoder_ready = 1;
    return BLUEDROID_SBC_OK;
}

static int prepare_encoder(bluedroid_sbc_t* sbc)
{
    if (!sbc)
    {
        return BLUEDROID_SBC_ERR;
    }

    if (sbc->encoder_ready)
    {
        return BLUEDROID_SBC_OK;
    }

    const uint8_t channels = channel_count(sbc);
    const uint8_t blocks   = block_count(sbc);
    const uint8_t bands    = subband_count(sbc);
    if (blocks == 0 || sample_rate_hz(sbc) == 0 || sbc->endian != BLUEDROID_SBC_LE)
    {
        return BLUEDROID_SBC_ERR;
    }

    memset(&sbc->encoder, 0, sizeof(sbc->encoder));
    sbc->encoder.s16SamplingFreq     = sbc->frequency;
    sbc->encoder.s16ChannelMode      = sbc->mode;
    sbc->encoder.s16NumOfSubBands    = bands;
    sbc->encoder.s16NumOfChannels    = channels;
    sbc->encoder.s16NumOfBlocks      = blocks;
    sbc->encoder.s16AllocationMethod = sbc->allocation;
    sbc->encoder.s16BitPool          = sbc->bitpool;
    sbc->encoder.u8NumPacketToEncode = 1;
    sbc->encoder.mSBCEnabled         = is_msbc(sbc) ? TRUE : FALSE;
    SBC_Encoder_Init(&sbc->encoder);
    sbc->encoder_ready = 1;
    return BLUEDROID_SBC_OK;
}

int bluedroid_sbc_init(bluedroid_sbc_t* sbc, unsigned long flags)
{
    if (!sbc)
    {
        return BLUEDROID_SBC_ERR;
    }

    memset(sbc, 0, sizeof(*sbc));
    sbc->flags      = flags;
    sbc->frequency  = BLUEDROID_SBC_FREQ_16000;
    sbc->mode       = BLUEDROID_SBC_MODE_MONO;
    sbc->allocation = BLUEDROID_SBC_AM_LOUDNESS;
    sbc->subbands   = BLUEDROID_SBC_SB_8;
    sbc->blocks     = BLUEDROID_SBC_BLK_15;
    sbc->bitpool    = 26;
    sbc->endian     = BLUEDROID_SBC_LE;
    return BLUEDROID_SBC_OK;
}

int bluedroid_sbc_reinit(bluedroid_sbc_t* sbc, unsigned long flags)
{
    return bluedroid_sbc_init(sbc, flags);
}

int bluedroid_sbc_configure_msbc(bluedroid_sbc_t* sbc)
{
    if (!sbc)
    {
        return BLUEDROID_SBC_ERR;
    }

    sbc->frequency     = BLUEDROID_SBC_FREQ_16000;
    sbc->mode          = BLUEDROID_SBC_MODE_MONO;
    sbc->allocation    = BLUEDROID_SBC_AM_LOUDNESS;
    sbc->subbands      = BLUEDROID_SBC_SB_8;
    sbc->blocks        = BLUEDROID_SBC_BLK_15;
    sbc->bitpool       = 26;
    sbc->endian        = BLUEDROID_SBC_LE;
    sbc->decoder_ready = 0;
    sbc->encoder_ready = 0;
    return BLUEDROID_SBC_OK;
}

ssize_t bluedroid_sbc_parse(bluedroid_sbc_t* sbc, const void* input, size_t input_len)
{
    if (!sbc || !input)
    {
        return BLUEDROID_SBC_ERR;
    }

    const size_t frame_length = bluedroid_sbc_get_frame_length(sbc);
    if (frame_length == 0 || input_len < frame_length)
    {
        return BLUEDROID_SBC_ERR;
    }

    return (ssize_t)frame_length;
}

ssize_t bluedroid_sbc_decode(bluedroid_sbc_t* sbc, const void* input, size_t input_len, void* output, size_t output_len, size_t* written)
{
    if (written)
    {
        *written = 0;
    }

    if (!sbc || !input || !output || !written || prepare_decoder(sbc) != BLUEDROID_SBC_OK)
    {
        return BLUEDROID_SBC_ERR;
    }

    const OI_BYTE* frame       = (const OI_BYTE*)input;
    OI_UINT32     frame_bytes = (OI_UINT32)input_len;
    OI_UINT32     pcm_bytes   = (OI_UINT32)output_len;
    OI_STATUS     status      = OI_CODEC_SBC_DecodeFrame(&sbc->decoder, &frame, &frame_bytes, (OI_INT16*)output, &pcm_bytes);
    if (!OI_SUCCESS(status))
    {
        return BLUEDROID_SBC_ERR;
    }

    *written = (size_t)pcm_bytes;
    return (ssize_t)(input_len - frame_bytes);
}

ssize_t bluedroid_sbc_encode(bluedroid_sbc_t* sbc, const void* input, size_t input_len, void* output, size_t output_len, ssize_t* written)
{
    if (written)
    {
        *written = 0;
    }

    if (!sbc || !input || !output || !written || prepare_encoder(sbc) != BLUEDROID_SBC_OK)
    {
        return BLUEDROID_SBC_ERR;
    }

    const size_t codesize     = bluedroid_sbc_get_codesize(sbc);
    const size_t frame_length = bluedroid_sbc_get_frame_length(sbc);
    if (codesize == 0 || frame_length == 0 || input_len < codesize || output_len < frame_length)
    {
        return BLUEDROID_SBC_ERR;
    }

    sbc->encoder.ps16PcmBuffer       = (SINT16*)input;
    sbc->encoder.pu8Packet           = (UINT8*)output;
    sbc->encoder.u16PacketLength     = 0;
    sbc->encoder.u8NumPacketToEncode = 1;
    SBC_Encoder(&sbc->encoder);

    if (sbc->encoder.u16PacketLength == 0 || sbc->encoder.u16PacketLength > output_len)
    {
        return BLUEDROID_SBC_ERR;
    }

    *written = (ssize_t)sbc->encoder.u16PacketLength;
    return (ssize_t)codesize;
}

size_t bluedroid_sbc_get_frame_length(bluedroid_sbc_t* sbc)
{
    if (!sbc)
    {
        return 0;
    }

    if (is_msbc(sbc))
    {
        return BLUEDROID_MSBC_FRAME_LENGTH;
    }

    OI_CODEC_SBC_FRAME_INFO frame;
    memset(&frame, 0, sizeof(frame));
    frame.frequency     = sbc->frequency;
    frame.mode          = sbc->mode;
    frame.subbands      = oi_subbands(sbc);
    frame.nrof_subbands = subband_count(sbc);
    frame.blocks        = oi_blocks(sbc);
    frame.nrof_blocks   = block_count(sbc);
    frame.alloc         = sbc->allocation;
    frame.bitpool       = sbc->bitpool;
    frame.nrof_channels = channel_count(sbc);
    return OI_CODEC_SBC_CalculateFramelen(&frame);
}

unsigned bluedroid_sbc_get_frame_duration(bluedroid_sbc_t* sbc)
{
    const uint32_t rate   = sample_rate_hz(sbc);
    const uint8_t  blocks = block_count(sbc);
    const uint8_t  bands  = subband_count(sbc);
    if (rate == 0 || blocks == 0 || bands == 0)
    {
        return 0;
    }

    return (unsigned)(((uint32_t)blocks * bands * 1000000UL) / rate);
}

size_t bluedroid_sbc_get_codesize(bluedroid_sbc_t* sbc)
{
    if (!sbc)
    {
        return 0;
    }

    const uint8_t blocks   = block_count(sbc);
    const uint8_t bands    = subband_count(sbc);
    const uint8_t channels = channel_count(sbc);
    return (size_t)blocks * bands * channels * sizeof(int16_t);
}

const char* bluedroid_sbc_get_implementation_info(bluedroid_sbc_t* sbc)
{
    (void)sbc;
    return "Bluedroid SBC/mSBC";
}

void bluedroid_sbc_finish(bluedroid_sbc_t* sbc)
{
    if (!sbc)
    {
        return;
    }

    sbc->decoder_ready = 0;
    sbc->encoder_ready = 0;
    memset(&sbc->decoder, 0, sizeof(sbc->decoder));
    memset(&sbc->encoder, 0, sizeof(sbc->encoder));
}
