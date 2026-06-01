#pragma once

#include "dbg_log.h"

#include <cstdint>

/*
use this file for preprocessor definitions that are used to configure parts of the code at compile-time
*/

//#define BUILD_CLOUD_FEATURES
//#define TEST_SIM_BATTERY
//#define TEST_BOOT_ERROR_NONFATAL
//#define TEST_BOOT_ERROR_FATAL
//#define TEST_MOCK_FW_UPDATE 1
//#define TEST_MOCK_MASTER_KEY
#define TEST_MOCK_PIN_CODE
#define TEST_MOCK_PASSWORD

#define BUILD_SILENCE_GAP_REMOVAL
#define BUILD_USE_MP3_COMPRESSION

#if !defined(BUILD_WITH_SECURITY_LEVEL)
#error BUILD_WITH_SECURITY_LEVEL must be defined!
#elif BUILD_WITH_SECURITY_LEVEL > 3
#error BUILD_WITH_SECURITY_LEVEL not valid
#endif

// storage stays the same, up to max, but for secure setups, only 1 is allowed, as we need to track enrollment
#ifdef BUILD_CLOUD_FEATURES
#define CLOUD_SERVER_CNT_MAX        8
#if BUILD_WITH_SECURITY_LEVEL >= 2
#define CLOUD_SERVER_CNT_ALLOWED    CLOUD_SERVER_CNT_MAX
#else
#define CLOUD_SERVER_CNT_ALLOWED    1
#endif
#else
#define CLOUD_SERVER_CNT_MAX        1
#define CLOUD_SERVER_CNT_ALLOWED    0
#endif

// storage stays the same, up to max, but allowed customizable soft-AP configurations varies based on security levels
#define SOFTAP_CUSTOM_CFG_CNT_MAX    4
#if BUILD_WITH_SECURITY_LEVEL <= 0
#define SOFTAP_CUSTOM_CFG_CNT        SOFTAP_CUSTOM_CFG_CNT_MAX
#elif BUILD_WITH_SECURITY_LEVEL == 1
#define SOFTAP_CUSTOM_CFG_CNT        1
#elif BUILD_WITH_SECURITY_LEVEL >= 2
#define SOFTAP_CUSTOM_CFG_CNT        0
#endif

#if BUILD_WITH_SECURITY_LEVEL == 1
#define BUILD_WITH_ENCRYPTED_PLAYBACK
#endif

#if BUILD_WITH_SECURITY_LEVEL >= 1
#define BUILD_WITH_DECRYPTED_DOWNLOAD
#endif

#define MOST_RECENT_FILES_MAX_FILES  20

// Recorder audio is 16 kHz, 16-bit stereo PCM before optional compression.
#define AUDIO_RECORDER_SAMPLE_RATE_HZ        16000
#define AUDIO_RECORDER_CHANNELS             2
#define AUDIO_RECORDER_BITS_PER_SAMPLE      16
#define AUDIO_RECORDER_FRAME_BYTES          (AUDIO_RECORDER_CHANNELS * (AUDIO_RECORDER_BITS_PER_SAMPLE / 8))

// 8192 bytes is approximately 128 ms worth of 16 kHz 16-bit stereo PCM data.
#define RECORDER_ENCRYPTED_CHUNK_NONCE_LENGTH 12
#define RECORDER_ENCRYPTED_CHUNK_TAG_LENGTH   16
#define RECORDER_ENCRYPTED_CHUNK_OVERHEAD     (RECORDER_ENCRYPTED_CHUNK_NONCE_LENGTH + RECORDER_ENCRYPTED_CHUNK_TAG_LENGTH)
#define WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH 8192

// At 16 kHz LAME emits MPEG-2 Layer III frames, which consume 576 PCM frames
// per channel. 64 kbps CBR gives 288-byte MP3 frames, so four MP3 frames is
// 144 ms of audio, 9216 bytes of source PCM, and 1152 bytes before encryption.
#define MP3_LAME_BITRATE_KBPS                64 // also set in `lib\arduino-liblame\src\MP3EncoderLAME.h`
#define MP3_LAME_PCM_FRAMES_PER_MP3_FRAME    576
#define MP3_LAME_MP3_FRAMES_PER_CHUNK        4
#define MP3_PCM_FRAMES_PER_CHUNK             (MP3_LAME_PCM_FRAMES_PER_MP3_FRAME * MP3_LAME_MP3_FRAMES_PER_CHUNK)
#define MP3_PCM_BYTES_PER_CHUNK              (MP3_PCM_FRAMES_PER_CHUNK * AUDIO_RECORDER_FRAME_BYTES)
#define MP3_CBR_BYTES_PER_SECOND             ((MP3_LAME_BITRATE_KBPS * 1000) / 8)
#define MP3_CBR_BYTES_PER_MP3_FRAME          ((72 * MP3_LAME_BITRATE_KBPS * 1000) / AUDIO_RECORDER_SAMPLE_RATE_HZ)
#define MP3_CBR_BYTES_PER_CHUNK              (MP3_CBR_BYTES_PER_MP3_FRAME * MP3_LAME_MP3_FRAMES_PER_CHUNK)          // 1152 for MP3_LAME_MP3_FRAMES_PER_CHUNK=4
#define MP3_CHUNK_DURATION_MS                ((MP3_PCM_FRAMES_PER_CHUNK * 1000) / AUDIO_RECORDER_SAMPLE_RATE_HZ)

// LAME's public buffer guidance is 1.25 * input frames + 7200 bytes. This is
// an encoder output-buffer capacity, not the size to encrypt or store.
#define MP3_LAME_OUTPUT_BUFFER_SIZE          (7200 + (((MP3_PCM_FRAMES_PER_CHUNK * 5) + 3) / 4))

// Ideal encrypted MP3 chunk sizing for this recorder configuration. Encrypt the
// actual CBR MP3 bytes from whole MP3 frames; keep MP3_LAME_OUTPUT_BUFFER_SIZE
// only as the temporary encoder buffer.
#define MP3_ENCRYPTED_PLAINTEXT_LENGTH       MP3_CBR_BYTES_PER_CHUNK
#define MP3_ENCRYPTED_CHUNK_LENGTH           (RECORDER_ENCRYPTED_CHUNK_NONCE_LENGTH + MP3_ENCRYPTED_PLAINTEXT_LENGTH + RECORDER_ENCRYPTED_CHUNK_TAG_LENGTH)
#define MP3_ENCRYPTED_BYTES_PER_SECOND       (MP3_CBR_BYTES_PER_SECOND + ((RECORDER_ENCRYPTED_CHUNK_OVERHEAD * 1000 + MP3_CHUNK_DURATION_MS - 1) / MP3_CHUNK_DURATION_MS))

#if defined(BUILD_USE_MP3_COMPRESSION)
#define AUDIO_RECORDER_PLAINTEXT_CHUNK_BYTES MP3_PCM_BYTES_PER_CHUNK
#else
#define AUDIO_RECORDER_PLAINTEXT_CHUNK_BYTES WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH
#endif

#if defined(DBG_LOG_LOCAL_LEVEL) && DBG_LOG_LOCAL_LEVEL > DBG_LOG_ERROR
#ifndef BUILD_IS_DEBUG
#define BUILD_IS_DEBUG
#endif
#endif

// file preallocation constants, not used
constexpr uint64_t kHalfGiB = 512ULL * 1024ULL * 1024ULL;
constexpr uint64_t kMaxGrowFileBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;

// #define ENABLE_FILE_PREALLOCATION
// #define ENABLE_HFP_AUDIO_DIAGNOSTICS

