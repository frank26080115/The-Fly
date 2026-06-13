// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "AudioFileRecorder.h"

#include <M5Unified.h>
#include <atomic>
#include <limits.h>
#include <mutex>
#include <new>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if BUILD_WITH_SECURITY_LEVEL >= 1
#include "Aegis.h"
#include "esp_random.h"
#include "mbedtls/gcm.h"
#endif

#include "ClockAgent.h"
#include "DiskStats.h"
#include "MicroSdCard.h"
#include "HapticsWrapper.h"
#include "dbg_log.h"
#include "defs.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utilfuncs.h"

#if defined(BUILD_USE_MP3_COMPRESSION)
#include "ShineWrapper.h"
#endif

namespace AudioFileRecorder
{
namespace // private
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr const char* TAG = "AudioFileRecorder";

constexpr uint32_t kPumpBudgetMs                = 20;
constexpr uint8_t  kPumpTargetFillPercentage    = 0;
constexpr float    kWriteDurationAverageAlpha   = 0.05f;
constexpr uint32_t kWriteDurationThresholdUs    = 10000;
constexpr uint32_t kTimedFlushIntervalMs        = 2000;
constexpr uint8_t  kMaxConsecutiveWriteFailures = 3;

constexpr uint32_t kWavSampleRateHz         = AUDIO_RECORDER_SAMPLE_RATE_HZ;
constexpr uint16_t kWavChannels             = AUDIO_RECORDER_CHANNELS;
constexpr uint16_t kWavBitsPerSample        = AUDIO_RECORDER_BITS_PER_SAMPLE;
constexpr size_t   kWavHeaderSize           = WAV_RIFF_HEADER_LENGTH;
constexpr size_t   kWavFrameBytes           = AUDIO_RECORDER_FRAME_BYTES;
constexpr size_t   kWavPlaintextChunkBytes  = WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH;
constexpr size_t   kMp3PcmFramesPerChunk    = MP3_PCM_FRAMES_PER_CHUNK;
constexpr size_t   kWavPumpFrames           = kWavPlaintextChunkBytes / kWavFrameBytes;
constexpr uint32_t kWavPlaceholderDataBytes = 0x7FFFFFFF;
constexpr uint32_t kWavMaxDataBytes         = 0xFFFFFFFFUL - 36UL;
#if defined(BUILD_USE_MP3_COMPRESSION)
constexpr size_t  kMp3PumpFrameCapacity         = MP3_ENCODER_MAX_SAMPLES_PER_PASS;
constexpr size_t  kRecordingPumpFrames          = kMp3PumpFrameCapacity;
constexpr size_t  kEncryptedAudioPlaintextBytes = MP3_ENCRYPTED_PLAINTEXT_LENGTH;
constexpr size_t  kEncryptedAudioChunkBytes     = MP3_ENCRYPTED_CHUNK_LENGTH;
constexpr uint8_t kMp3CloseSilencePumpLimit     = 8;
#else
constexpr size_t kRecordingPumpFrames          = kWavPumpFrames;
constexpr size_t kEncryptedAudioPlaintextBytes = WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH;
constexpr size_t kEncryptedAudioChunkBytes     = WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH;
#endif

static_assert(kWavPlaintextChunkBytes % kWavFrameBytes == 0,
              "recorder plaintext chunks must contain whole stereo frames");
#if defined(BUILD_USE_MP3_COMPRESSION)
static_assert(kMp3PumpFrameCapacity <= kMp3PcmFramesPerChunk,
              "MP3 pump frames must fit inside one configured MP3 PCM chunk");
#endif
static_assert(MP3_PCM_BYTES_PER_CHUNK % kWavFrameBytes == 0, "MP3 PCM chunks must contain whole stereo frames");
static_assert(MP3_PCM_FRAMES_PER_CHUNK % MP3_PCM_FRAMES_PER_MP3_FRAME == 0,
              "MP3 PCM chunks must contain whole MP3 frames");
static_assert(WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH == WAV_ENCRYPTED_CHUNK_NONCE_LENGTH +
                                                      WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH +
                                                      WAV_ENCRYPTED_CHUNK_TAG_LENGTH,
              "encrypted WAV audio chunk length must match nonce + ciphertext + tag");
static_assert(MP3_ENCRYPTED_CHUNK_LENGTH == RECORDER_ENCRYPTED_CHUNK_NONCE_LENGTH + MP3_ENCRYPTED_PLAINTEXT_LENGTH +
                                                RECORDER_ENCRYPTED_CHUNK_TAG_LENGTH,
              "encrypted MP3 chunk length must match nonce + ciphertext + tag");
static_assert(WAV_ENCRYPTED_RIFF_HEADER_LENGTH ==
                  WAV_ENCRYPTED_CHUNK_NONCE_LENGTH + WAV_RIFF_HEADER_LENGTH + WAV_ENCRYPTED_CHUNK_TAG_LENGTH,
              "encrypted RIFF header length must match nonce + ciphertext + tag");
static_assert(WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH >= WAV_ENCRYPTED_RIFF_HEADER_LENGTH,
              "encrypted audio chunk buffer must also hold the encrypted RIFF header");
static_assert(kEncryptedAudioPlaintextBytes <= WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH,
              "encrypted plaintext staging buffer must hold the selected recording chunk");
static_assert(kEncryptedAudioChunkBytes <= WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH,
              "encrypted output staging buffer must hold the selected recording chunk");

#if BUILD_WITH_SECURITY_LEVEL >= 1
constexpr size_t kGcmNonceSize = WAV_ENCRYPTED_CHUNK_NONCE_LENGTH;
constexpr size_t kGcmTagSize   = WAV_ENCRYPTED_CHUNK_TAG_LENGTH;
#endif
constexpr size_t kWavPlaintextAudioBufferBytes = WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH;
#if BUILD_WITH_SECURITY_LEVEL >= 1 || defined(BUILD_WITH_ENCRYPTED_PLAYBACK)
constexpr size_t kWavEncryptedAudioBufferBytes = WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH;
#endif

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

#if defined(BUILD_USE_MP3_COMPRESSION)
using Mp3EncoderType = ShineWrapper;
#endif

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

AudioFifo*            g_host_fifo = nullptr;
AudioFifo*            g_mic_fifo  = nullptr;
FsFile                g_file;
char                  g_sd_path[48]                  = {};
char                  g_recording_type_code          = static_cast<char>(RecordingType::Unknown);
MemoType              g_memo_type                    = MEMO_TYPE_NOTE;
uint64_t              g_bytes_written                = 0;
uint32_t              g_last_flush_ms                = 0;
std::atomic<bool>     g_recording                    = {false};
bool                  g_write_duration_average_valid = false;
float                 g_write_duration_average_us    = 0.0f;
uint32_t              g_write_duration_max_us        = 0;
uint32_t              g_last_longwrite_ms            = 0;
bool                  g_longwrite                    = false;
bool                  g_longwrite_latched            = false;
uint8_t               g_consecutive_write_failures   = 0;
bool                  g_card_failure_reported        = false;
uint8_t               g_consecutive_silent_chunks    = 0;
std::atomic<uint32_t> g_fifo_overflow_events         = {0};
bool                  g_host_fifo_overflow_latched   = false;
bool                  g_mic_fifo_overflow_latched    = false;
#if defined(BUILD_USE_MP3_COMPRESSION)
Mp3EncoderType*   g_mp3_encoder             = nullptr;
std::atomic<bool> g_mp3_callback_failed     = {false};
bool              g_mp3_file_write_observed = false;
size_t            g_mp3_samples_per_pass    = MP3_PCM_FRAMES_PER_MP3_FRAME;
#endif
#if BUILD_WITH_SECURITY_LEVEL >= 1
mbedtls_gcm_context g_recording_gcm;
bool                g_recording_gcm_ready        = false;
uint32_t            g_encryption_sequence        = 0;
size_t              g_plaintext_audio_chunk_used = 0;
#endif
std::mutex g_recorder_mutex;
std::mutex g_pump_mutex;
std::mutex g_audio_buffer_mutex;
int16_t    g_wav_mic_samples[kRecordingPumpFrames];
int16_t    g_wav_host_samples[kRecordingPumpFrames];
#if defined(BUILD_USE_MP3_COMPRESSION)
int16_t g_mp3_stereo_samples[kMp3PumpFrameCapacity * kWavChannels];
#endif
uint8_t* g_wav_plaintext_audio      = nullptr;
size_t   g_wav_plaintext_audio_size = 0;
#if BUILD_WITH_SECURITY_LEVEL >= 1 || defined(BUILD_WITH_ENCRYPTED_PLAYBACK)
uint8_t* g_wav_encrypted_audio      = nullptr;
size_t   g_wav_encrypted_audio_size = 0;
#endif

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

uint64_t max_prealloc_size();
void     make_recording_path(char type_code, uint8_t attempt);
bool     memo_type_from_code(char type_code, MemoType& type);
bool     recording_type_is_memo(char type_code);
char*    basename_for_path(char* path);
bool     make_path_with_type_code(const char* source_path, char type_code, char* out_path, size_t out_path_size);
bool     rename_stopped_recording(char* stopped_path, size_t stopped_path_size, char type_code);
bool     recording_active();
void     set_recording_active(bool active);
bool     ensure_audio_buffers_locked();
void     release_audio_buffers_locked();

#if BUILD_WITH_SECURITY_LEVEL >= 1
void store_u32_be(uint8_t* dst, uint32_t value);
void stop_recording_encryption_locked();
bool start_recording_encryption_locked();
void encrypted_chunk_nonce_locked(uint8_t nonce[kGcmNonceSize]);
bool encrypt_chunk_locked(const uint8_t* plaintext,
                          size_t         plaintext_size,
                          uint8_t*       encrypted,
                          size_t         encrypted_capacity,
                          size_t&        encrypted_size);
#endif

void build_wav_header(uint8_t header[kWavHeaderSize], uint32_t data_bytes);
void write_le16(uint8_t* dst, uint16_t value);
void write_le32(uint8_t* dst, uint32_t value);
bool write_wav_header_locked(uint32_t data_bytes);
bool write_placeholder_wav_header_locked();
bool finish_wav_header_locked();

#if BUILD_WITH_SECURITY_LEVEL >= 1
bool write_encrypted_placeholder_wav_header_locked();
#endif

void fatal_recording_storage_failure(const char* context, const char* readyMessage);
void update_write_duration_stats(uint32_t duration_us);
void flush_if_due();
bool recording_file_ready_locked();
bool recording_file_ready();
void note_write_success_locked();
bool is_silent_chunk(const int16_t* samples, size_t sample_count);
bool note_write_failure_locked();
void reset_fifo_overflow_tracking();
void note_fifo_overflow_event(AudioFifo* fifo, bool& latched);
void note_fifo_overflow_events();
void reset_fifo_flow_flags_after_observation();

#if BUILD_WITH_SECURITY_LEVEL >= 1
bool   write_full_encrypted_audio_chunk_locked(bool& encryption_failed, bool& storage_failed);
bool   append_encrypted_audio_locked(const uint8_t* data,
                                     size_t         byte_count,
                                     bool&          encryption_failed,
                                     bool&          storage_failed);
bool   finish_encrypted_audio_locked();
size_t plaintext_audio_write_offset();
bool   encrypted_audio_chunk_full();
bool   retry_full_encrypted_audio_chunk();
#else
size_t plaintext_audio_write_offset();
#endif

#if defined(BUILD_USE_MP3_COMPRESSION)
void destroy_mp3_encoder_locked();
void mp3_data_callback(uint8_t* data, size_t byte_count);
bool start_mp3_encoder_locked();
bool write_mp3_encoded_data(uint8_t* data, size_t byte_count);
bool write_mp3_samples(int16_t* samples, size_t byte_count);
bool flush_mp3_encoder();
#endif

bool write_samples(const int16_t* samples, size_t byte_count);
bool pump_audio_chunk_locked(bool force);
bool fifos_below_pump_target();
void set_queue_enabled(bool enabled);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

bool init(AudioFifo& hostFifo, AudioFifo& micFifo)
{
    {
        std::lock_guard<std::mutex> lock(g_recorder_mutex);
        g_host_fifo = &hostFifo;
        g_mic_fifo  = &micFifo;
    }
    return true;
}

bool ensureAudioBuffers()
{
    std::lock_guard<std::mutex> lock(g_audio_buffer_mutex);
    return ensure_audio_buffers_locked();
}

void releaseAudioBuffers()
{
    std::lock_guard<std::mutex> lock(g_audio_buffer_mutex);
    if (recording_active())
    {
        DBG_LOGW(TAG, "audio buffers still in use; release skipped");
        return;
    }

    release_audio_buffers_locked();
}

bool startRecording(RecordingType type)
{
    return startRecording(static_cast<char>(type));
}

bool startRecording(char typeCode)
{
    if (!g_host_fifo || !g_mic_fifo)
    {
        DBG_LOGE(TAG, "not initialized, data sources not set");
        return false;
    }

    if (isRecording())
    {
        // if already recording, stop recording and start a new file
        stopRecording();
    }

    if (!MicroSdCard::isReady())
    {
        DBG_LOGE(TAG, "microSD card is not ready");
        show_fatal_error_f(true, "microSD card is missing or unreadable");
        return false;
    }

#if !defined(BUILD_USE_MP3_COMPRESSION) || BUILD_WITH_SECURITY_LEVEL >= 1
    if (!ensureAudioBuffers())
    {
        DBG_LOGE(TAG, "audio buffer allocation failed");
        show_fatal_error_f(true, "recording audio buffer allocation failed");
        return false;
    }
#endif

    g_host_fifo->setQueueEnabled(false);
    g_mic_fifo->setQueueEnabled(false);
    g_host_fifo->clear();
    g_mic_fifo->clear();

    char started_path[sizeof(g_sd_path)] = {};
    bool recording_file_opened           = false;
    bool encryption_setup_failed         = false;
    bool wav_header_failed               = false;
#if defined(BUILD_USE_MP3_COMPRESSION)
    bool mp3_encoder_setup_failed = false;
#endif
    {
        std::lock_guard<std::mutex> lock(g_recorder_mutex);

        // make the new file name and open it
        make_recording_path(typeCode, 0);
        SdFs& fs = MicroSdCard::fs();
        if (fs.exists(g_sd_path))
        {
            make_recording_path(typeCode, 1);
            if (fs.exists(g_sd_path))
            {
                make_recording_path(typeCode, 3);
            }
        }

        if (!g_file.open(g_sd_path, O_RDWR | O_CREAT | O_TRUNC))
        {
            DBG_LOGE(TAG, "open failed: %s", g_sd_path);
        }
        else
        {
#if BUILD_WITH_SECURITY_LEVEL >= 1
            if (!start_recording_encryption_locked())
            {
                g_file.close();
                g_sd_path[0]            = '\0';
                encryption_setup_failed = true;
            }
#endif

            if (!encryption_setup_failed)
            {
#ifdef ENABLE_FILE_PREALLOCATION
                // preallocate file space to avoid unexpected latency for editing file table entry
                grow_file(g_file, max_prealloc_size());
#endif

                g_bytes_written              = 0;
                g_last_flush_ms              = millis();
                g_consecutive_write_failures = 0;
                g_card_failure_reported      = false;
                g_consecutive_silent_chunks  = 0;
                reset_fifo_overflow_tracking();
                g_recording_type_code = typeCode;
                memo_type_from_code(typeCode, g_memo_type);
#if defined(BUILD_USE_MP3_COMPRESSION)
                g_mp3_callback_failed.store(false, std::memory_order_relaxed);
                g_mp3_file_write_observed = false;
#endif
#if BUILD_WITH_SECURITY_LEVEL >= 1
                g_encryption_sequence        = 0;
                g_plaintext_audio_chunk_used = 0;
#endif

#if defined(BUILD_USE_MP3_COMPRESSION)
                if (!start_mp3_encoder_locked())
                {
                    g_file.close();
                    g_sd_path[0]             = '\0';
                    mp3_encoder_setup_failed = true;
                }
                else
                {
                    set_recording_active(true);
                    recording_file_opened = true;
                    strncpy(started_path, g_sd_path, sizeof(started_path) - 1);
                }
#else
#if BUILD_WITH_SECURITY_LEVEL >= 1
                if (!write_encrypted_placeholder_wav_header_locked())
#else
                if (!write_placeholder_wav_header_locked())
#endif
                {
                    DBG_LOGE(TAG, "WAV header write failed: %s", g_sd_path);
                    g_file.close();
                    g_sd_path[0]      = '\0';
                    wav_header_failed = true;
                }
                else
                {
                    set_recording_active(true);
                    recording_file_opened = true;
                    strncpy(started_path, g_sd_path, sizeof(started_path) - 1);
                }
#endif
            }
        }
    }
    if (encryption_setup_failed)
    {
        show_fatal_error_f(true, "recording encryption setup failed");
        return false;
    }
    if (!recording_file_opened)
    {
#if BUILD_WITH_SECURITY_LEVEL >= 1
        std::lock_guard<std::mutex> lock(g_recorder_mutex);
        stop_recording_encryption_locked();
#endif
#if defined(BUILD_USE_MP3_COMPRESSION)
        if (mp3_encoder_setup_failed)
        {
            show_fatal_error_f(true, "MP3 encoder setup failed");
            return false;
        }
#endif
        fatal_recording_storage_failure(
            wav_header_failed ? "writing WAV header" : "opening recording file",
            wav_header_failed ? "microSD WAV header write failed" : "microSD recording file open failed");
        return false;
    }

    set_queue_enabled(true); // actually start recording
    DBG_LOGI(TAG, "recording started: %s", started_path);
    return true;
}

void setRecordingType(RecordingType type)
{
    setRecordingType(static_cast<char>(type));
}

void setRecordingType(char typeCode)
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    g_recording_type_code = typeCode;
    memo_type_from_code(typeCode, g_memo_type);
}

void setMemoType(MemoType type)
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    g_memo_type = type;
    if (recording_type_is_memo(g_recording_type_code))
    {
        g_recording_type_code = memo_type_to_code(type);
    }
}

bool needsPump()
{
    return recording_active();
}

void pump()
{
    Diagnostics::memory_check_in();

    if (!needsPump())
    {
        return;
    }

    std::unique_lock<std::mutex> pump_lock(g_pump_mutex, std::try_to_lock);
    if (!pump_lock.owns_lock())
    {
        return;
    }

    flush_if_due();

    if (!recording_file_ready())
    {
        return;
    }

    g_longwrite            = false;
    const uint32_t started = millis();
    // pump until no data or until too much time has passed
    do
    {
        if (!pump_audio_chunk_locked(false))
        {
            return;
        }
    } while (!fifos_below_pump_target() && (millis() - started) < kPumpBudgetMs);
}

bool stopRecording(bool estop)
{
    note_fifo_overflow_events();
    set_queue_enabled(false); // tells the queues to stop recording

    {
        std::lock_guard<std::mutex> lock(g_recorder_mutex);
        if (!recording_active() && !g_file)
        {
            return true;
        }
    }

    bool                        ok                              = true;
    char                        stopped_path[sizeof(g_sd_path)] = {};
    uint64_t                    stopped_bytes                   = 0;
    bool                        closed_file                     = false;
    std::lock_guard<std::mutex> pump_lock(g_pump_mutex);
    if (!estop)
    {
        // drain the last bit of the FIFO out, there is less pressure now since the FIFOs have been signalled to stop
        // queuing
        while (pump_audio_chunk_locked(true))
        {
            taskYIELD();
        }
    }

#if defined(BUILD_USE_MP3_COMPRESSION)
    if (!flush_mp3_encoder())
    {
        DBG_LOGE(TAG, "MP3 encoder flush failed");
        ok = false;
    }
#endif

    char     stopped_type_code = static_cast<char>(RecordingType::Unknown);
    MemoType stopped_memo_type = MEMO_TYPE_NOTE;
    {
        std::lock_guard<std::mutex> lock(g_recorder_mutex);
        if (!recording_active() && !g_file)
        {
            return true;
        }

        stopped_type_code = g_recording_type_code;
        stopped_memo_type = g_memo_type;
    }

    {
        std::lock_guard<std::mutex> lock(g_recorder_mutex);
        if (!recording_active() && !g_file)
        {
            return true;
        }

        set_recording_active(false);
        strncpy(stopped_path, g_sd_path, sizeof(stopped_path) - 1);

        if (g_file)
        {
#if defined(BUILD_USE_MP3_COMPRESSION)
#if BUILD_WITH_SECURITY_LEVEL >= 1
            if (!finish_encrypted_audio_locked())
            {
                ok = false;
            }
#endif
#else
#if BUILD_WITH_SECURITY_LEVEL >= 1
            if (!finish_encrypted_audio_locked())
            {
                ok = false;
            }
#else
            if (!finish_wav_header_locked())
            {
                ok = false;
            }
#endif
#endif

            g_file.flush(); // this makes sure the buffer actually makes it onto the card
            g_last_flush_ms = millis();

            #ifdef ENABLE_FILE_PREALLOCATION
            // shrink the grown file to what is required
            if (!g_file.truncate(g_bytes_written))
            {
                ok = false;
            }
            #endif

            g_file.close();
            closed_file = true;
        }

        stopped_bytes = g_bytes_written;

#if defined(BUILD_USE_MP3_COMPRESSION)
        destroy_mp3_encoder_locked();
#endif
#if BUILD_WITH_SECURITY_LEVEL >= 1
        stop_recording_encryption_locked();
#endif
    }

    if (closed_file)
    {
        if (recording_type_is_memo(stopped_type_code))
        {
            rename_stopped_recording(stopped_path, sizeof(stopped_path), memo_type_to_code(stopped_memo_type));
        }

        DiskStats::refreshDiskSpace();
    }

    if (!ok)
    {
        DBG_LOGE(TAG, "recording close failed");
        return false;
    }

    haptic_play_buzz();

    DBG_LOGI(TAG, "recording stopped: %s bytes=%llu", stopped_path, static_cast<unsigned long long>(stopped_bytes));
    return true;
}

// -----------------------------------------------------------------------------
// State and Diagnostics
// -----------------------------------------------------------------------------

bool isRecording()
{
    return recording_active();
}

bool purePcmMode()
{
    return false;
}

void setPurePcmMode(bool enabled)
{
    (void)enabled;
}

float writeDurationAverageMs()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    return g_write_duration_average_us / 1000.0f;
}

float writeDurationMaxMs()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    return static_cast<float>(g_write_duration_max_us) / 1000.0f;
}

void resetWriteDurationStats()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    g_write_duration_average_valid = false;
    g_write_duration_average_us    = 0.0f;
    g_write_duration_max_us        = 0;
}

bool longWrite()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    return g_longwrite;
}

bool longWriteSinceReset()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    return g_longwrite_latched;
}

void resetLongWrite()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    g_longwrite = false;
}

void resetLongWriteSinceReset()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    g_longwrite_latched = false;
}

uint32_t lastLongWriteTimestampMs()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    return g_last_longwrite_ms;
}

uint64_t bytesWritten()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    return g_bytes_written;
}

uint32_t fifoOverflowEvents()
{
    return g_fifo_overflow_events.load(std::memory_order_relaxed);
}

const char* currentSdPath()
{
    return g_sd_path;
}

bool grow_file(FsFile& file, uint64_t size)
{
    // growing a file means making the file gigantic at creation
    // this is so that while writing, there's no need to stop the writing in order to update the file table (or whatever
    // mechanism is tracking file size) resulting in a significant performance increase for file writing, lower latency,
    // less unexpected cache flushes the file will be truncated when it is closed

    if (size > kMaxGrowFileBytes)
    {
        // don't grow too big, we are limited by FAT32 maximum file size, and obviously, available space
        size = kMaxGrowFileBytes;
    }

    // only allocate in chunks of 0.5GB
    size = (size / kHalfGiB) * kHalfGiB;

    // we attempt to allocate as much as possible, but if it is refused, step down the size
    while (size >= kHalfGiB)
    {
        if (file.preAllocate(size))
        {
            file.rewind(); // reset seek pointer to beginning
            DBG_LOGI(TAG, "file reserved %llu bytes", static_cast<unsigned long long>(size));
            return true;
        }

        DBG_LOGW(TAG, "file reserve failed at %llu bytes", static_cast<unsigned long long>(size));
        size -= kHalfGiB;
    }

    // failed? allow to continue but report error
    DBG_LOGW(TAG, "file reserve failed completely");
    file.rewind();
    return false;
}

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
uint8_t* wavPlaintextAudioBuffer()
{
    if (!ensureAudioBuffers())
    {
        return nullptr;
    }
    return g_wav_plaintext_audio;
}

uint8_t* wavEncryptedAudioBuffer()
{
    if (!ensureAudioBuffers())
    {
        return nullptr;
    }
    return g_wav_encrypted_audio;
}

size_t wavPlaintextAudioBufferSize()
{
    std::lock_guard<std::mutex> lock(g_audio_buffer_mutex);
    return g_wav_plaintext_audio ? g_wav_plaintext_audio_size : 0;
}

size_t wavEncryptedAudioBufferSize()
{
    std::lock_guard<std::mutex> lock(g_audio_buffer_mutex);
    return g_wav_encrypted_audio ? g_wav_encrypted_audio_size : 0;
}
#endif

namespace // private
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

bool allocate_audio_buffer_locked(uint8_t*& buffer, size_t& size, size_t required_size, const char* name)
{
    if (buffer && size >= required_size)
    {
        return true;
    }

    if (buffer)
    {
        heap_caps_free(buffer);
        buffer = nullptr;
        size   = 0;
    }

    buffer = reinterpret_cast<uint8_t*>(heap_caps_malloc(required_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer)
    {
        DBG_LOGE(TAG, "PSRAM allocation failed for %s buffer: %u bytes", name, static_cast<unsigned>(required_size));
        return false;
    }

    size = required_size;
    DBG_LOGI(TAG, "allocated %s audio buffer in PSRAM: %u bytes", name, static_cast<unsigned>(size));
    return true;
}

void release_audio_buffer_locked(uint8_t*& buffer, size_t& size, const char* name)
{
    if (!buffer)
    {
        size = 0;
        return;
    }

    memset(buffer, 0, size);
    heap_caps_free(buffer);
    DBG_LOGI(TAG, "released %s audio buffer: %u bytes", name, static_cast<unsigned>(size));
    buffer = nullptr;
    size   = 0;
}

bool ensure_audio_buffers_locked()
{
    if (!allocate_audio_buffer_locked(g_wav_plaintext_audio,
                                      g_wav_plaintext_audio_size,
                                      kWavPlaintextAudioBufferBytes,
                                      "plaintext"))
    {
        return false;
    }

#if BUILD_WITH_SECURITY_LEVEL >= 1 || defined(BUILD_WITH_ENCRYPTED_PLAYBACK)
    if (!allocate_audio_buffer_locked(g_wav_encrypted_audio,
                                      g_wav_encrypted_audio_size,
                                      kWavEncryptedAudioBufferBytes,
                                      "encrypted"))
    {
        release_audio_buffer_locked(g_wav_plaintext_audio, g_wav_plaintext_audio_size, "plaintext");
        return false;
    }
#endif

    return true;
}

void release_audio_buffers_locked()
{
    release_audio_buffer_locked(g_wav_plaintext_audio, g_wav_plaintext_audio_size, "plaintext");
#if BUILD_WITH_SECURITY_LEVEL >= 1 || defined(BUILD_WITH_ENCRYPTED_PLAYBACK)
    release_audio_buffer_locked(g_wav_encrypted_audio, g_wav_encrypted_audio_size, "encrypted");
#endif
}

uint64_t max_prealloc_size()
{
    uint64_t free_bytes = MicroSdCard::freeBytes();

    if (free_bytes > kMaxGrowFileBytes)
    {
        free_bytes = kMaxGrowFileBytes;
    }

    uint64_t size = (free_bytes / kHalfGiB) * kHalfGiB; // preallocated size is in multiples of 0.5GB

    return size;
}

void make_recording_path(char type_code, uint8_t attempt)
{
    m5::rtc_datetime_t now = {};
    if (!Clock.getDateTime(&now))
    {
        now.date.year    = 2026;
        now.date.month   = 1;
        now.date.date    = 1;
        now.time.hours   = 0;
        now.time.minutes = 0;
        now.time.seconds = 0;
    }
#if defined(BUILD_USE_MP3_COMPRESSION) && BUILD_WITH_SECURITY_LEVEL >= 1
    const char* extension = "fly";
#elif defined(BUILD_USE_MP3_COMPRESSION)
    const char* extension = "mp3";
#elif BUILD_WITH_SECURITY_LEVEL >= 1
    const char* extension = "rec";
#else
    const char* extension = "wav";
#endif

    if (attempt == 0)
    {
        snprintf(g_sd_path,
             sizeof(g_sd_path),
             "/%c-%04d-%02d-%02d-%02d-%02d.%s",
             type_code,
             now.date.year,
             now.date.month,
             now.date.date,
             now.time.hours,
             now.time.minutes,
             extension);
    }
    else if (attempt == 1)
    {
        tm next_minute = {};
        next_minute.tm_year = now.date.year - 1900;
        next_minute.tm_mon  = now.date.month - 1;
        next_minute.tm_mday = now.date.date;
        next_minute.tm_hour = now.time.hours;
        next_minute.tm_min  = now.time.minutes + 1;
        next_minute.tm_sec  = now.time.seconds;

        const time_t normalized_time = mktime(&next_minute);
        if (normalized_time != static_cast<time_t>(-1))
        {
            tm normalized = {};
            if (localtime_r(&normalized_time, &normalized))
            {
                now.date.year    = normalized.tm_year + 1900;
                now.date.month   = normalized.tm_mon + 1;
                now.date.date    = normalized.tm_mday;
                now.time.hours   = normalized.tm_hour;
                now.time.minutes = normalized.tm_min;
                now.time.seconds = normalized.tm_sec;
            }
        }
        else
        {
            now.time.minutes = static_cast<uint8_t>((now.time.minutes + 1) % 60);
            if (now.time.minutes == 0)
            {
                now.time.hours = static_cast<uint8_t>((now.time.hours + 1) % 24);
            }
        }

        snprintf(g_sd_path,
             sizeof(g_sd_path),
             "/%c-%04d-%02d-%02d-%02d-%02d.%s",
             type_code,
             now.date.year,
             now.date.month,
             now.date.date,
             now.time.hours,
             now.time.minutes,
             extension);
    }
    else
    {
        snprintf(g_sd_path,
             sizeof(g_sd_path),
             "/%c-%04d-%02d-%02d-%02d-%02d-%02d.%s",
             type_code,
             now.date.year,
             now.date.month,
             now.date.date,
             now.time.hours,
             now.time.minutes,
             now.time.seconds,
             extension);
    }
}

bool memo_type_from_code(char type_code, MemoType& type)
{
    switch (type_code)
    {
    case 'T':
        type = MEMO_TYPE_TODO;
        return true;
    case 'J':
        type = MEMO_TYPE_JOURNAL;
        return true;
    case 'I':
        type = MEMO_TYPE_IDEA;
        return true;
    case 'R':
        type = MEMO_TYPE_REMINDER;
        return true;
    case 'M':
        type = MEMO_TYPE_NOTE;
        return true;
    default:
        return false;
    }
}

bool recording_type_is_memo(char type_code)
{
    MemoType ignored = MEMO_TYPE_NOTE;
    return memo_type_from_code(type_code, ignored);
}

char* basename_for_path(char* path)
{
    if (!path)
    {
        return nullptr;
    }

    char* name = path;
    for (char* cursor = path; *cursor; ++cursor)
    {
        if (*cursor == '/' || *cursor == '\\')
        {
            name = cursor + 1;
        }
    }

    return name;
}

bool make_path_with_type_code(const char* source_path, char type_code, char* out_path, size_t out_path_size)
{
    if (!source_path || !out_path || out_path_size == 0)
    {
        return false;
    }

    strncpy(out_path, source_path, out_path_size - 1);
    out_path[out_path_size - 1] = '\0';

    char* name = basename_for_path(out_path);
    if (!name || name[0] == '\0' || name[0] == type_code)
    {
        return false;
    }

    name[0] = type_code;
    return strcmp(out_path, source_path) != 0;
}

bool rename_stopped_recording(char* stopped_path, size_t stopped_path_size, char type_code)
{
    char renamed_path[sizeof(g_sd_path)] = {};
    if (!make_path_with_type_code(stopped_path, type_code, renamed_path, sizeof(renamed_path)))
    {
        return false;
    }

    SdFs& fs = MicroSdCard::fs();
    if (fs.exists(renamed_path))
    {
        DBG_LOGW(TAG, "recording rename skipped, target exists: %s", renamed_path);
        return false;
    }

    if (!fs.rename(stopped_path, renamed_path))
    {
        DBG_LOGW(TAG, "recording rename failed: %s -> %s", stopped_path, renamed_path);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_recorder_mutex);
        strncpy(g_sd_path, renamed_path, sizeof(g_sd_path) - 1);
        g_sd_path[sizeof(g_sd_path) - 1] = '\0';
    }

    strncpy(stopped_path, renamed_path, stopped_path_size - 1);
    stopped_path[stopped_path_size - 1] = '\0';
    DBG_LOGI(TAG, "recording renamed: %s", stopped_path);
    return true;
}

bool recording_file_ready_locked();

bool recording_active()
{
    return g_recording.load(std::memory_order_relaxed);
}

void set_recording_active(bool active)
{
    g_recording.store(active, std::memory_order_relaxed);
}

#if BUILD_WITH_SECURITY_LEVEL >= 1
void store_u32_be(uint8_t* dst, uint32_t value)
{
    dst[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    dst[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dst[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dst[3] = static_cast<uint8_t>(value & 0xFF);
}

void stop_recording_encryption_locked()
{
    if (g_recording_gcm_ready)
    {
        mbedtls_gcm_free(&g_recording_gcm);
        g_recording_gcm_ready = false;
    }
}

bool start_recording_encryption_locked()
{
    stop_recording_encryption_locked();

    if (!Aegis::isInitialized())
    {
        DBG_LOGE(TAG, "Aegis is not initialized");
        return false;
    }

    const uint8_t* filecrypt_key = Aegis::getFilecryptKey();
    if (!filecrypt_key)
    {
        DBG_LOGE(TAG, "Aegis filecrypt key is not available");
        return false;
    }

    mbedtls_gcm_init(&g_recording_gcm);
    if (mbedtls_gcm_setkey(&g_recording_gcm, MBEDTLS_CIPHER_ID_AES, filecrypt_key, Aegis::kFilecryptKeySize * 8) != 0)
    {
        mbedtls_gcm_free(&g_recording_gcm);
        DBG_LOGE(TAG, "recording AES-GCM setup failed");
        return false;
    }

    g_recording_gcm_ready = true;
    return true;
}

// AES-GCM needs a unique 12-byte nonce for every chunk encrypted with the same key.
// Store the nonce beside each ciphertext chunk so later playback can decrypt directly.
void encrypted_chunk_nonce_locked(uint8_t nonce[kGcmNonceSize])
{
    store_u32_be(nonce, esp_random());
    store_u32_be(nonce + 4, esp_random());
    store_u32_be(nonce + 8, g_encryption_sequence++);
}

bool encrypt_chunk_locked(const uint8_t* plaintext,
                          size_t         plaintext_size,
                          uint8_t*       encrypted,
                          size_t         encrypted_capacity,
                          size_t&        encrypted_size)
{
    encrypted_size = 0;
    if (!g_recording_gcm_ready)
    {
        return false;
    }
    if (!plaintext || !encrypted)
    {
        return false;
    }

    const size_t required_size = kGcmNonceSize + plaintext_size + kGcmTagSize;
    if (encrypted_capacity < required_size)
    {
        return false;
    }

    uint8_t* nonce      = encrypted;
    uint8_t* ciphertext = encrypted + kGcmNonceSize;
    uint8_t* tag        = ciphertext + plaintext_size;

    encrypted_chunk_nonce_locked(nonce);
    if (mbedtls_gcm_crypt_and_tag(&g_recording_gcm,
                                  MBEDTLS_GCM_ENCRYPT,
                                  plaintext_size,
                                  nonce,
                                  kGcmNonceSize,
                                  nullptr,
                                  0,
                                  plaintext,
                                  ciphertext,
                                  kGcmTagSize,
                                  tag) != 0)
    {
        return false;
    }

    encrypted_size = required_size;
    return true;
}
#endif

void write_le16(uint8_t* dst, uint16_t value)
{
    dst[0] = static_cast<uint8_t>(value & 0xFF);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void write_le32(uint8_t* dst, uint32_t value)
{
    dst[0] = static_cast<uint8_t>(value & 0xFF);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dst[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dst[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void build_wav_header(uint8_t header[kWavHeaderSize], uint32_t data_bytes)
{
    memset(header, 0, kWavHeaderSize);

    memcpy(header + 0, "RIFF", 4);
    write_le32(header + 4, data_bytes + 36);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    write_le32(header + 16, 16);
    write_le16(header + 20, 1);
    write_le16(header + 22, kWavChannels);
    write_le32(header + 24, kWavSampleRateHz);
    write_le32(header + 28, kWavSampleRateHz * kWavChannels * (kWavBitsPerSample / 8));
    write_le16(header + 32, kWavChannels * (kWavBitsPerSample / 8));
    write_le16(header + 34, kWavBitsPerSample);
    memcpy(header + 36, "data", 4);
    write_le32(header + 40, data_bytes);
}

bool write_wav_header_locked(uint32_t data_bytes)
{
    uint8_t header[kWavHeaderSize] = {};

    build_wav_header(header, data_bytes);
    return g_file.seekSet(0) && g_file.write(header, sizeof(header)) == sizeof(header);
}

bool write_placeholder_wav_header_locked()
{
    if (!write_wav_header_locked(kWavPlaceholderDataBytes))
    {
        return false;
    }
    if (!g_file.seekSet(kWavHeaderSize))
    {
        return false;
    }

    g_bytes_written = kWavHeaderSize;
    return true;
}

bool finish_wav_header_locked()
{
    if (g_bytes_written < kWavHeaderSize)
    {
        return false;
    }

    const uint64_t data_bytes = g_bytes_written - kWavHeaderSize;
    if (data_bytes > kWavMaxDataBytes)
    {
        DBG_LOGE(TAG, "WAV data is too large: %llu bytes", static_cast<unsigned long long>(data_bytes));
        return false;
    }

    return write_wav_header_locked(static_cast<uint32_t>(data_bytes)) && g_file.seekSet(g_bytes_written);
}

#if BUILD_WITH_SECURITY_LEVEL >= 1
bool write_encrypted_placeholder_wav_header_locked()
{
    if (!g_wav_encrypted_audio || g_wav_encrypted_audio_size < WAV_ENCRYPTED_RIFF_HEADER_LENGTH)
    {
        return false;
    }

    uint8_t header[kWavHeaderSize] = {};
    build_wav_header(header, kWavPlaceholderDataBytes);

    size_t encrypted_size = 0;
    if (!encrypt_chunk_locked(header,
                              sizeof(header),
                              g_wav_encrypted_audio,
                              g_wav_encrypted_audio_size,
                              encrypted_size))
    {
        return false;
    }
    if (encrypted_size != WAV_ENCRYPTED_RIFF_HEADER_LENGTH)
    {
        return false;
    }
    if (!g_file.seekSet(0) || g_file.write(g_wav_encrypted_audio, encrypted_size) != encrypted_size)
    {
        return false;
    }

    g_bytes_written              = encrypted_size;
    g_plaintext_audio_chunk_used = 0;
    return true;
}
#endif

void fatal_recording_storage_failure(const char* context, const char* readyMessage)
{
    const MicroSdCard::Health health = MicroSdCard::health();
    DBG_LOGE(TAG,
             "microSD recording storage failure while %s: health=%s",
             context ? context : "starting recording",
             MicroSdCard::healthName(health));

    switch (health)
    {
    case MicroSdCard::Health::NotReady:
    case MicroSdCard::Health::MissingOrUnreadable:
        show_fatal_error_f(true, "microSD card is missing or unreadable");
        break;
    case MicroSdCard::Health::Full:
        show_fatal_error_f(true, "microSD card is full");
        break;
    case MicroSdCard::Health::Ready:
    default:
        show_fatal_error_f(true, "%s", readyMessage ? readyMessage : "microSD recording storage failed");
        break;
    }
}

void update_write_duration_stats(uint32_t duration_us)
{
    const bool exceeded_threshold = duration_us > kWriteDurationThresholdUs;
    if (exceeded_threshold)
    {
        Diagnostics::long_write_exceeded();
        g_longwrite         = true; // this is to be set false at the top of every pump
        g_longwrite_latched = true;
        g_last_longwrite_ms = millis();
    }

    if (!g_write_duration_average_valid)
    {
        g_write_duration_average_us    = static_cast<float>(duration_us);
        g_write_duration_average_valid = true;
    }
    else
    {
        g_write_duration_average_us +=
            (static_cast<float>(duration_us) - g_write_duration_average_us) * kWriteDurationAverageAlpha;
    }

    if (duration_us > g_write_duration_max_us)
    {
        g_write_duration_max_us = duration_us;
    }
}

void flush_if_due()
{
    const uint32_t              now_ms = millis();
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    if (!recording_file_ready_locked())
    {
        return;
    }
    if (static_cast<uint32_t>(now_ms - g_last_flush_ms) < kTimedFlushIntervalMs)
    {
        return;
    }

    g_file.flush();
    g_last_flush_ms = millis();
}

bool recording_file_ready_locked()
{
    return recording_active() && g_file;
}

bool recording_file_ready()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    return recording_file_ready_locked();
}

void note_write_success_locked()
{
    g_consecutive_write_failures = 0;
}

bool is_silent_chunk(const int16_t* samples, size_t sample_count)
{
    if (!samples || sample_count == 0)
    {
        return false;
    }

    constexpr int16_t kSilentSampleAbsLevel = 0;
    for (size_t i = 0; i < sample_count; ++i)
    {
        const int32_t sample   = samples[i];
        const int32_t absolute = sample < 0 ? -sample : sample;
        if (absolute > kSilentSampleAbsLevel)
        {
            return false;
        }
    }

    return true;
}

bool note_write_failure_locked()
{
    if (g_consecutive_write_failures < kMaxConsecutiveWriteFailures)
    {
        ++g_consecutive_write_failures;
    }

    if (g_consecutive_write_failures < kMaxConsecutiveWriteFailures || g_card_failure_reported)
    {
        return false;
    }

    g_card_failure_reported = true;
    set_recording_active(false);
    return true;
}

void reset_fifo_overflow_tracking()
{
    g_fifo_overflow_events.store(0, std::memory_order_relaxed);
    g_host_fifo_overflow_latched = false;
    g_mic_fifo_overflow_latched  = false;
    if (g_host_fifo)
    {
        g_host_fifo->resetFlowFlags();
    }
    if (g_mic_fifo)
    {
        g_mic_fifo->resetFlowFlags();
    }
}

void note_fifo_overflow_event(AudioFifo* fifo, bool& latched)
{
    if (!fifo)
    {
        return;
    }

    const bool overflowed = fifo->overflowed();
    if (overflowed && !latched)
    {
        g_fifo_overflow_events.fetch_add(1, std::memory_order_relaxed);
    }
    latched = overflowed;
}

void note_fifo_overflow_events()
{
    note_fifo_overflow_event(g_host_fifo, g_host_fifo_overflow_latched);
    note_fifo_overflow_event(g_mic_fifo, g_mic_fifo_overflow_latched);
}

void reset_fifo_flow_flags_after_observation()
{
    if (g_host_fifo)
    {
        g_host_fifo->resetFlowFlags();
    }
    if (g_mic_fifo)
    {
        g_mic_fifo->resetFlowFlags();
    }
    g_host_fifo_overflow_latched = false;
    g_mic_fifo_overflow_latched  = false;
}

#if BUILD_WITH_SECURITY_LEVEL >= 1
bool write_full_encrypted_audio_chunk_locked(bool& encryption_failed, bool& storage_failed)
{
    encryption_failed = false;
    storage_failed    = false;

    if (!g_wav_plaintext_audio || !g_wav_encrypted_audio || g_wav_plaintext_audio_size < kEncryptedAudioPlaintextBytes ||
        g_wav_encrypted_audio_size < kEncryptedAudioChunkBytes)
    {
        encryption_failed = true;
        return false;
    }

    if (g_plaintext_audio_chunk_used != kEncryptedAudioPlaintextBytes)
    {
        encryption_failed = true;
        return false;
    }

    size_t encrypted_size = 0;
    if (!encrypt_chunk_locked(g_wav_plaintext_audio,
                              kEncryptedAudioPlaintextBytes,
                              g_wav_encrypted_audio,
                              g_wav_encrypted_audio_size,
                              encrypted_size) ||
        encrypted_size != kEncryptedAudioChunkBytes)
    {
        encryption_failed = true;
        return false;
    }

    if (g_file.write(g_wav_encrypted_audio, encrypted_size) != encrypted_size)
    {
        storage_failed = true;
        return false;
    }

    g_bytes_written += encrypted_size;
    g_plaintext_audio_chunk_used = 0;
#if defined(BUILD_USE_MP3_COMPRESSION)
    g_mp3_file_write_observed = true;
#endif
    return true;
}

bool append_encrypted_audio_locked(const uint8_t* data,
                                   size_t         byte_count,
                                   bool&          encryption_failed,
                                   bool&          storage_failed)
{
    encryption_failed = false;
    storage_failed    = false;

    while (byte_count > 0)
    {
        if (!g_wav_plaintext_audio || g_wav_plaintext_audio_size < kEncryptedAudioPlaintextBytes)
        {
            encryption_failed = true;
            return false;
        }

        const size_t space = kEncryptedAudioPlaintextBytes - g_plaintext_audio_chunk_used;
        if (space == 0)
        {
            if (!write_full_encrypted_audio_chunk_locked(encryption_failed, storage_failed))
            {
                return false;
            }
            continue;
        }

        const size_t copy_bytes = byte_count < space ? byte_count : space;
        uint8_t*     dst        = g_wav_plaintext_audio + g_plaintext_audio_chunk_used;
        if (data != dst)
        {
            memmove(dst, data, copy_bytes);
        }

        g_plaintext_audio_chunk_used += copy_bytes;
        data += copy_bytes;
        byte_count -= copy_bytes;

        if (g_plaintext_audio_chunk_used == kEncryptedAudioPlaintextBytes &&
            !write_full_encrypted_audio_chunk_locked(encryption_failed, storage_failed))
        {
            return false;
        }
    }

    return true;
}

bool finish_encrypted_audio_locked()
{
    if (g_plaintext_audio_chunk_used == 0)
    {
        return true;
    }

    if (!g_wav_plaintext_audio || g_wav_plaintext_audio_size < kEncryptedAudioPlaintextBytes)
    {
        return false;
    }

    memset(g_wav_plaintext_audio + g_plaintext_audio_chunk_used,
           0,
           kEncryptedAudioPlaintextBytes - g_plaintext_audio_chunk_used);
    g_plaintext_audio_chunk_used = kEncryptedAudioPlaintextBytes;

    bool encryption_failed = false;
    bool storage_failed    = false;
    if (!write_full_encrypted_audio_chunk_locked(encryption_failed, storage_failed))
    {
        DBG_LOGE(TAG,
                 "%s",
                 encryption_failed ? "final encrypted audio chunk encryption failed"
                                   : "final encrypted audio chunk write failed");
        return false;
    }

    return true;
}

size_t plaintext_audio_write_offset()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    return g_plaintext_audio_chunk_used;
}

bool encrypted_audio_chunk_full()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    return g_plaintext_audio_chunk_used == kEncryptedAudioPlaintextBytes;
}

bool retry_full_encrypted_audio_chunk()
{
    std::unique_lock<std::mutex> lock(g_recorder_mutex);
    if (!recording_file_ready_locked() || g_plaintext_audio_chunk_used != kEncryptedAudioPlaintextBytes)
    {
        return false;
    }

    bool encryption_failed = false;
    bool storage_failed    = false;
    if (!write_full_encrypted_audio_chunk_locked(encryption_failed, storage_failed))
    {
        if (encryption_failed)
        {
            DBG_LOGE(TAG, "audio chunk encryption retry failed");
            set_recording_active(false);
            lock.unlock();
            show_fatal_error_f(true, "recording encryption failed");
            return false;
        }

        DBG_LOGE(TAG, "encrypted samples retry write failed");
        const bool fatal_card_failure = note_write_failure_locked();
        lock.unlock();
        if (fatal_card_failure)
        {
            fatal_recording_storage_failure("retrying encrypted recording samples", "microSD recording write failed");
        }
        return false;
    }

    note_write_success_locked();
    return true;
}
#else
size_t plaintext_audio_write_offset()
{
    return 0;
}
#endif

#if defined(BUILD_USE_MP3_COMPRESSION)
void destroy_mp3_encoder_locked()
{
    delete g_mp3_encoder;
    g_mp3_encoder = nullptr;
}

void mp3_data_callback(uint8_t* data, size_t byte_count)
{
    if (!write_mp3_encoded_data(data, byte_count))
    {
        g_mp3_callback_failed.store(true, std::memory_order_relaxed);
    }
}

bool start_mp3_encoder_locked()
{
    destroy_mp3_encoder_locked();
    g_mp3_samples_per_pass = MP3_PCM_FRAMES_PER_MP3_FRAME;

    g_mp3_encoder = new (std::nothrow) ShineWrapper(AUDIO_RECORDER_SAMPLE_RATE_HZ,
                                                    AUDIO_RECORDER_CHANNELS,
                                                    AUDIO_RECORDER_BITS_PER_SAMPLE,
                                                    MP3_ENCODER_QUALITY,
                                                    MP3_BITRATE_KBPS,
                                                    true,
                                                    mp3_data_callback);
    if (!g_mp3_encoder)
    {
        DBG_LOGE(TAG, "MP3 encoder allocation failed");
        return false;
    }

    if (!g_mp3_encoder->begin())
    {
        DBG_LOGE(TAG, "MP3 encoder setup failed");
        destroy_mp3_encoder_locked();
        return false;
    }

    const int samples_per_pass = g_mp3_encoder->getSamplesPerPass();
    if (samples_per_pass <= 0 || static_cast<size_t>(samples_per_pass) > kMp3PumpFrameCapacity)
    {
        DBG_LOGE(TAG, "MP3 encoder samples-per-pass is invalid: %d", samples_per_pass);
        destroy_mp3_encoder_locked();
        return false;
    }
    g_mp3_samples_per_pass = static_cast<size_t>(samples_per_pass);

    return true;
}

bool write_mp3_encoded_data(uint8_t* data, size_t byte_count)
{
    if (!data || byte_count == 0)
    {
        return true;
    }

    std::unique_lock<std::mutex> lock(g_recorder_mutex);
    if (!recording_file_ready_locked())
    {
        return false;
    }

    const uint32_t started_us = micros();

#if BUILD_WITH_SECURITY_LEVEL >= 1
    bool encryption_failed = false;
    bool storage_failed    = false;
    if (!append_encrypted_audio_locked(data, byte_count, encryption_failed, storage_failed))
    {
        if (encryption_failed)
        {
            DBG_LOGE(TAG, "MP3 chunk encryption failed");
            set_recording_active(false);
            lock.unlock();
            show_fatal_error_f(true, "recording encryption failed");
            return false;
        }

        DBG_LOGE(TAG, "encrypted MP3 chunk write failed");
        const bool fatal_card_failure = note_write_failure_locked();
        lock.unlock();
        if (fatal_card_failure)
        {
            fatal_recording_storage_failure("writing encrypted MP3 recording data", "microSD recording write failed");
        }
        return false;
    }
#else
    if (g_file.write(data, byte_count) != byte_count)
    {
        DBG_LOGE(TAG, "MP3 data write failed");
        const bool fatal_card_failure = note_write_failure_locked();
        lock.unlock();
        if (fatal_card_failure)
        {
            fatal_recording_storage_failure("writing MP3 recording data", "microSD recording write failed");
        }
        return false;
    }

    g_bytes_written += byte_count;
    g_mp3_file_write_observed = true;
#endif

    note_write_success_locked();
    update_write_duration_stats(static_cast<uint32_t>(micros() - started_us));
    return true;
}

bool write_mp3_samples(int16_t* samples, size_t byte_count)
{
    Mp3EncoderType* encoder = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_recorder_mutex);
        if (!recording_file_ready_locked() || !g_mp3_encoder)
        {
            return false;
        }
        g_mp3_callback_failed.store(false, std::memory_order_relaxed);
        encoder = g_mp3_encoder;
    }

    if (byte_count > static_cast<size_t>(INT_MAX))
    {
        return false;
    }

    const int32_t written = encoder->write(samples, static_cast<int>(byte_count));
    if (g_mp3_callback_failed.load(std::memory_order_relaxed))
    {
        return false;
    }

    return written == static_cast<int32_t>(byte_count);
}

bool flush_mp3_encoder()
{
    Mp3EncoderType* encoder = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_recorder_mutex);
        if (!g_mp3_encoder)
        {
            return true;
        }
        g_mp3_callback_failed.store(false, std::memory_order_relaxed);
        encoder = g_mp3_encoder;
    }

    const bool flushed = encoder->flush();
    return flushed && !g_mp3_callback_failed.load(std::memory_order_relaxed);
}
#endif

bool write_samples(const int16_t* samples, size_t byte_count)
{
    std::unique_lock<std::mutex> lock(g_recorder_mutex);
    if (!recording_file_ready_locked())
    {
        return false;
    }

    if (!samples || byte_count == 0)
    {
        return false;
    }

    const uint32_t started_us = micros();
    const uint8_t* data_ptr   = reinterpret_cast<const uint8_t*>(samples);

#if BUILD_WITH_SECURITY_LEVEL >= 1
    bool encryption_failed = false;
    bool storage_failed    = false;
    if (!append_encrypted_audio_locked(data_ptr, byte_count, encryption_failed, storage_failed))
    {
        if (encryption_failed)
        {
            DBG_LOGE(TAG, "audio chunk encryption failed");
            set_recording_active(false);
            lock.unlock();
            show_fatal_error_f(true, "recording encryption failed");
            return false;
        }

        DBG_LOGE(TAG, "encrypted samples write failed");
        const bool fatal_card_failure = note_write_failure_locked();
        lock.unlock();
        if (fatal_card_failure)
        {
            fatal_recording_storage_failure("writing encrypted recording samples", "microSD recording write failed");
        }
        return false;
    }
#else
    if (g_file.write(data_ptr, byte_count) != byte_count)
    {
        DBG_LOGE(TAG, "samples write failed");
        const bool fatal_card_failure = note_write_failure_locked();
        lock.unlock();
        if (fatal_card_failure)
        {
            fatal_recording_storage_failure("writing recording samples", "microSD recording write failed");
        }
        return false;
    }
    // assume automatic cache flushing will happen when required, do not call flush manually

    g_bytes_written += byte_count;
#endif

    note_write_success_locked();

    update_write_duration_stats(static_cast<uint32_t>(micros() - started_us));

    return true;
}

bool pump_audio_chunk_locked(bool force)
{
    if (!g_host_fifo || !g_mic_fifo || !recording_file_ready())
    {
        return false;
    }

    note_fifo_overflow_events();

#if BUILD_WITH_SECURITY_LEVEL >= 1
    if (encrypted_audio_chunk_full())
    {
        return retry_full_encrypted_audio_chunk();
    }
#endif

    const size_t mic_available  = force ? g_mic_fifo->usedSamples() : g_mic_fifo->availableToRead();
    const size_t host_available = force ? g_host_fifo->usedSamples() : g_host_fifo->availableToRead();

    if (force)
    {
        if (mic_available == 0 && host_available == 0)
        {
            return false;
        }
    }
    else if (mic_available == 0 || host_available == 0)
    {
        return false;
    }

    const size_t paired_frames = mic_available < host_available ? mic_available : host_available;
    const size_t forced_frames = mic_available > host_available ? mic_available : host_available;
    const size_t wanted_frames = force ? forced_frames : paired_frames;

#if defined(BUILD_USE_MP3_COMPRESSION)
    const size_t mp3_pump_frames = g_mp3_samples_per_pass > 0 ? g_mp3_samples_per_pass : MP3_PCM_FRAMES_PER_MP3_FRAME;
    if (!force && wanted_frames < mp3_pump_frames)
    {
        return false;
    }
    const size_t frames = wanted_frames < mp3_pump_frames ? wanted_frames : mp3_pump_frames;
#else
    if (!g_wav_plaintext_audio || g_wav_plaintext_audio_size == 0)
    {
        return false;
    }
    const size_t buffer_offset = plaintext_audio_write_offset();
    if (buffer_offset >= g_wav_plaintext_audio_size)
    {
        return false;
    }
    const size_t buffer_frames = (g_wav_plaintext_audio_size - buffer_offset) / kWavFrameBytes;
    const size_t max_frames    = buffer_frames < kWavPumpFrames ? buffer_frames : kWavPumpFrames;
    const size_t frames        = wanted_frames < max_frames ? wanted_frames : max_frames;
#endif
    if (frames == 0)
    {
        return false;
    }

    size_t mic_read = 0;
    if (mic_available > 0)
    {
        mic_read = force ? g_mic_fifo->dequeueMonoImmediate(g_wav_mic_samples, frames)
                         : g_mic_fifo->dequeueMono(g_wav_mic_samples, frames);
    }
    if (mic_read < frames)
    {
        memset(g_wav_mic_samples + mic_read, 0, (frames - mic_read) * sizeof(g_wav_mic_samples[0]));
    }

    size_t host_read = 0;
    if (host_available > 0)
    {
        host_read = force ? g_host_fifo->dequeueMonoImmediate(g_wav_host_samples, frames)
                          : g_host_fifo->dequeueMono(g_wav_host_samples, frames);
    }
    if (host_read < frames)
    {
        memset(g_wav_host_samples + host_read, 0, (frames - host_read) * sizeof(g_wav_host_samples[0]));
    }

    if (!force && (mic_read == 0 || host_read == 0))
    {
        return false;
    }

#if defined(BUILD_USE_MP3_COMPRESSION)
    int16_t* stereo_samples = g_mp3_stereo_samples;
#else
    int16_t* stereo_samples = reinterpret_cast<int16_t*>(g_wav_plaintext_audio + buffer_offset);
#endif
    for (size_t i = 0; i < frames; ++i)
    {
        stereo_samples[i * 2]     = g_wav_mic_samples[i];
        stereo_samples[i * 2 + 1] = g_wav_host_samples[i];
    }

#ifdef BUILD_SILENCE_GAP_REMOVAL
    if (is_silent_chunk(stereo_samples, frames * kWavChannels))
    {
        if (g_consecutive_silent_chunks >=
#if defined(BUILD_USE_MP3_COMPRESSION)
            20
#else
            5
#endif
        )
        {
            return true;
        }
        ++g_consecutive_silent_chunks;
    }
    else
    {
        g_consecutive_silent_chunks = 0;
    }
#endif

#if defined(BUILD_USE_MP3_COMPRESSION)
    if (!write_mp3_samples(stereo_samples, frames * kWavFrameBytes))
    {
        return false;
    }
#else
    if (!write_samples(stereo_samples, frames * kWavFrameBytes))
    {
        return false;
    }
#endif

    reset_fifo_flow_flags_after_observation();

    return true;
}

bool fifos_below_pump_target()
{
    // during file recording, the file recording task has more priority than the GUI task
    // so we want to always drain the FIFOs as much as possible
    // but maybe not completely if kPumpTargetFillPercentage is not zero, this is configurable

    if (!g_host_fifo || !g_mic_fifo)
    {
        return true;
    }

    return g_host_fifo->getFillPercentage() <= kPumpTargetFillPercentage &&
           g_mic_fifo->getFillPercentage() <= kPumpTargetFillPercentage;
}

void set_queue_enabled(bool enabled)
{
    if (g_host_fifo)
    {
        g_host_fifo->setQueueEnabled(enabled);
    }
    if (g_mic_fifo)
    {
        g_mic_fifo->setQueueEnabled(enabled);
    }
}

} // namespace

} // namespace AudioFileRecorder
