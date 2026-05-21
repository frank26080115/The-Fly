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
    #ifdef BUILD_WITH_SECURITY
    // we want 12 bytes of nonce, the timestamp and seq-num takes care of 8 of them, we can do another 8 for 16 bytes of uniqueness
    uint32_t nonce_1;        // random
    uint32_t nonce_2;        // random
    #endif
    uint32_t fifo_cnt;       // count before dequeue
    uint16_t payload_length; // length of payload, up to the max allowed
    uint16_t payload[FILE_PACKET_PAYLOAD_MAX];

    #ifdef BUILD_WITH_SECURITY_AUTHENTICATE
    // authentication signature
    uint8_t auth_tag[SECURITY_AUTH_TAG_SIZE];
    #endif
}
file_packet_t;

// strings can be parsed into one of these internally stored icons
enum : uint8_t
{
    ICON_UNKNOWN = 0,
    ICON_PHONE,
    ICON_LAPTOP,
    ICON_TABLET,
    ICON_BLUETOOTH,
    ICON_WIFI,
    ICON_WIFI_SEARCH,
    ICON_HOME,
    ICON_WIFIAP,
    ICON_WWW,
    ICON_CLOUD,
    ICON_FTP,
    ICON_NTP,
    ICON_INFO,
    ICON_BTPAIRING,
    ICON_PHONE_AP,
    ICON_LAST
};

#ifdef __cplusplus
enum class MemoType : uint8_t
{
    Note,
    Todo,
    Journal,
    Idea,
    Reminder,
};
#endif
