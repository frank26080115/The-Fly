#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
use this for things like enumerations, structures
*/

#define RESET_MAGIC    0x51A7C0DE

enum
{
    AUDSRC_BT,
    AUDSRC_MIC,
};

#define FILE_PACKET_HEADER_MAGIC 0xDEADBEEF
#define FILE_PACKET_PAYLOAD_MAX 256

// this structure is a fixed size, which makes everything less complicated, and enables easier scrubbing
typedef struct __attribute__((packed))
{
    uint32_t magic;  // always FILE_PACKET_HEADER_MAGIC, makes it easier to sync
    uint8_t src:4;   // indicates where the audio came from, AUDSRC_BT or AUDSRC_MIC
    uint8_t flags:4; // indicates any warnings from the FIFO
    #ifdef FILE_CONTAINS_DEBUG
    uint32_t ms_timestamp; // millis() when written to file
    uint32_t sequence_num;
    uint32_t fifo_cnt;     // count before dequeue
    #endif
    uint16_t payload_length; // length of payload, up to the max allowed
    uint16_t payload[FILE_PACKET_PAYLOAD_MAX];
}
file_packet_t;
