#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "conf.h"

/*
use this for things like enumerations, structures
*/

#define RESET_MAGIC    0x51A7C0DE

#define WAV_RIFF_HEADER_LENGTH 44
#define WAV_ENCRYPTED_CHUNK_NONCE_LENGTH RECORDER_ENCRYPTED_CHUNK_NONCE_LENGTH
#define WAV_ENCRYPTED_CHUNK_TAG_LENGTH RECORDER_ENCRYPTED_CHUNK_TAG_LENGTH
#define WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH (WAV_ENCRYPTED_CHUNK_NONCE_LENGTH + WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH + WAV_ENCRYPTED_CHUNK_TAG_LENGTH)
#define WAV_ENCRYPTED_RIFF_HEADER_LENGTH (WAV_ENCRYPTED_CHUNK_NONCE_LENGTH + WAV_RIFF_HEADER_LENGTH + WAV_ENCRYPTED_CHUNK_TAG_LENGTH)

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
