#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
use this for things like enumerations, structures
*/

#define RESET_MAGIC    0x51A7C0DE

#define FILE_PACKET_HEADER_MAGIC 0xDEADBEEF
#define FILE_PACKET_PAYLOAD_MAX 256

typedef enum : uint8_t
{
    AUDSRC_BT_16KHZ_MONO     = 0,
    AUDSRC_MIC_16KHZ_MONO    = 1,
    AUDSRC_BT_32KHZ_MONO     = 2,
    AUDSRC_MIC_32KHZ_MONO    = 3,
    AUDSRC_BT_32KHZ_STEREO   = 4,
    AUDSRC_MIC_32KHZ_STEREO  = 5,
    AUDSRC_BT_48KHZ_MONO     = 6,
    AUDSRC_MIC_48KHZ_MONO    = 7,
    AUDSRC_BT_48KHZ_STEREO   = 8,
    AUDSRC_MIC_48KHZ_STEREO  = 9,
    AUDSRC_META_TEXT         = 0xAA,
}
filepkt_src_e;

// this structure is a fixed size, which makes everything less complicated, and enables easier scrubbing
typedef struct __attribute__((packed))
{
    uint32_t magic;          // always FILE_PACKET_HEADER_MAGIC, makes it easier to sync
    filepkt_src_e src;       // indicates where the audio came from (or a special type)
    uint8_t  flags;          // indicates any warnings from the FIFO
    uint32_t ms_timestamp;   // millis() when written to file
    uint32_t sequence_num;   // per whole file, not per channel/FIFO
    #if BUILD_WITH_SECURITY_LEVEL >= 1
    // we want 12 bytes of nonce, the timestamp and seq-num takes care of 8 of them, we can do another 8 for 16 bytes of uniqueness
    uint32_t nonce_1;        // random
    uint32_t nonce_2;        // random
    #endif
    uint32_t fifo_cnt;       // count before dequeue
    uint16_t payload_length; // length of payload, up to the max allowed
    uint16_t payload[FILE_PACKET_PAYLOAD_MAX];
}
file_packet_t;

// strings can be parsed into one of these internally stored icons
enum : uint8_t
{
    ICON_UNKNOWN = 0,
    ICON_SMARTPHONE,
    ICON_LAPTOP,
    ICON_TABLET,
    ICON_HOME,
    ICON_WORK,
    ICON_CAR,
    ICON_PLANE,
    ICON_CAT,
    ICON_DOG,
    ICON_BIRD,
    ICON_CIRCLE,
    ICON_SQUARE,
    ICON_TRIANGLE,
    ICON_LAST
};

typedef enum
{
    MEMO_TYPE_NOTE = 0,
    MEMO_TYPE_TODO,
    MEMO_TYPE_JOURNAL,
    MEMO_TYPE_IDEA,
    MEMO_TYPE_REMINDER,
} MemoType;

#define WEBASSET_FILENAME_LEN 64
#define WEBASSET_MIMETYPE_LEN 64

typedef struct
{
    char file_name[WEBASSET_FILENAME_LEN];
    char mime_type[WEBASSET_MIMETYPE_LEN];
    uint32_t uncompressed_size;
    uint32_t compressed_size;
    const uint8_t* ptr_payload;
}
web_asset_desc_t;
