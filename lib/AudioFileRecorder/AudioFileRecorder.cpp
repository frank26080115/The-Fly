#include "AudioFileRecorder.h"

#include <M5Unified.h>
#include <mutex>
#include <stdio.h>
#include <string.h>

#if BUILD_WITH_SECURITY_LEVEL >= 1
#include "Aegis.h"
#include "esp_random.h"
#include "mbedtls/gcm.h"
#endif

#include "ClockAgent.h"
#include "CallManager.h"
#include "DiskStats.h"
#include "MicroSdCard.h"
#include "dbg_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utilfuncs.h"

namespace AudioFileRecorder
{
namespace // private
{

constexpr const char* TAG = "AudioFileRecorder";

constexpr uint32_t kPumpBudgetMs             = 20;
constexpr uint8_t  kPumpTargetFillPercentage = 0;    // keep this at zero, draining the FIFO fully is the best way of letting the host decoder know that there was a gap in the data
constexpr float    kWriteDurationAverageAlpha       = 0.05f;
constexpr uint32_t kWriteDurationThresholdUs        = 10000;
constexpr uint32_t kTimedFlushIntervalMs            = 2000;
constexpr size_t   kMetaTextPayloadMaxBytes         = FILE_PACKET_PAYLOAD_MAX * sizeof(uint16_t);
constexpr uint8_t  kMaxConsecutiveWriteFailures     = 3;

#if BUILD_WITH_SECURITY_LEVEL >= 1
constexpr size_t kGcmNonceSize = 12;
constexpr size_t kGcmTagSize   = 16;

struct encrypted_file_packet_t
{
    uint8_t nonce[kGcmNonceSize];
    uint8_t ciphertext[sizeof(file_packet_t)];
    uint8_t tag[kGcmTagSize];
};

static_assert(sizeof(encrypted_file_packet_t) == kGcmNonceSize + sizeof(file_packet_t) + kGcmTagSize,
              "encrypted file packet must not contain padding");
#endif

AudioFifo* g_host_fifo = nullptr;
AudioFifo* g_mic_fifo  = nullptr;
FsFile     g_file;
char       g_sd_path[48]                            = {};
char       g_queued_meta_text[kMetaTextPayloadMaxBytes + 1] = {};
size_t     g_queued_meta_text_length                = 0;
bool       g_meta_text_queued                       = false;
char       g_recording_type_code                    = static_cast<char>(RecordingType::Unknown);
MemoType   g_memo_type                              = MEMO_TYPE_NOTE;
uint64_t   g_bytes_written                          = 0;
uint32_t   g_sequence_num                           = 0;
uint32_t   g_last_flush_ms                          = 0;
bool       g_recording                              = false;
bool       g_pure_pcm_mode                          = false; // for testing only
bool       g_write_duration_average_valid           = false;
float      g_write_duration_average_us              = 0.0f;
uint32_t   g_write_duration_max_us                  = 0;
uint32_t   g_last_longwrite_ms                      = 0;
bool       g_longwrite                              = false;
bool       g_longwrite_latched                      = false;
bool       g_next_source_toggle                     = true;
uint8_t    g_consecutive_write_failures             = 0;
bool       g_card_failure_reported                  = false;
#if BUILD_WITH_SECURITY_LEVEL >= 1
mbedtls_gcm_context g_packet_gcm;
bool                g_packet_gcm_ready              = false;
#endif
std::mutex g_recorder_mutex;
std::mutex g_pump_mutex;

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

void make_recording_path(char type_code)
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
    snprintf(g_sd_path, sizeof(g_sd_path), "/%c-%04d-%02d-%02d-%02d-%02d-%02d-U.%s", type_code, now.date.year, now.date.month, now.date.date, now.time.hours, now.time.minutes, now.time.seconds, g_pure_pcm_mode ? "pcm" : "rec");
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

bool recording_type_is_call(char type_code)
{
    return type_code == static_cast<char>(RecordingType::Meeting);
}

bool recording_type_is_memo(char type_code)
{
    MemoType ignored = MEMO_TYPE_NOTE;
    return memo_type_from_code(type_code, ignored);
}

void clear_queued_meta_text_locked()
{
    g_queued_meta_text[0]     = '\0';
    g_queued_meta_text_length = 0;
    g_meta_text_queued        = false;
}

bool prepare_stop_meta_text(char type_code, MemoType memo_type)
{
    char text[kMetaTextPayloadMaxBytes + 1] = {};
    bool text_fits = true;

    if (recording_type_is_call(type_code))
    {
        text_fits = CallManager::formatCallMetaText(text, sizeof(text));
    }
    else if (recording_type_is_memo(type_code))
    {
        snprintf(text, sizeof(text), "memo-type:%s", memo_type_to_string(memo_type));
    }

    if (text[0] == '\0')
    {
        return true;
    }

    if (!text_fits)
    {
        DBG_LOGW(TAG, "recording metadata text was truncated");
    }

    if (!queueMetaText(text))
    {
        DBG_LOGW(TAG, "recording metadata text was too long for one packet");
        return false;
    }

    return true;
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

uint8_t flags_for_fifo(AudioFifo& fifo)
{
    uint8_t flags = 0;
    if (fifo.overflowed())
    {
        flags |= kPacketFlagFifoOverflow;
    }
    if (fifo.underflowed())
    {
        flags |= kPacketFlagFifoUnderflow;
    }
    return flags;
}

void reset_fifo_flags(AudioFifo& fifo)
{
    fifo.resetFlowFlags();
}

bool recording_file_ready_locked();

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
    if (g_packet_gcm_ready)
    {
        mbedtls_gcm_free(&g_packet_gcm);
        g_packet_gcm_ready = false;
    }
}

bool start_recording_encryption_locked()
{
    stop_recording_encryption_locked();

    if (g_pure_pcm_mode)
    {
        return true;
    }

    if (!Aegis::isInitialized() && !Aegis::init())
    {
        DBG_LOGE(TAG, "Aegis init failed");
        return false;
    }

    const uint8_t* filecrypt_key = Aegis::getFilecryptKey();
    if (!filecrypt_key)
    {
        DBG_LOGE(TAG, "Aegis filecrypt key is not available");
        return false;
    }

    mbedtls_gcm_init(&g_packet_gcm);
    if (mbedtls_gcm_setkey(&g_packet_gcm, MBEDTLS_CIPHER_ID_AES, filecrypt_key, Aegis::kFilecryptKeySize * 8) != 0)
    {
        mbedtls_gcm_free(&g_packet_gcm);
        DBG_LOGE(TAG, "recording AES-GCM setup failed");
        return false;
    }

    g_packet_gcm_ready = true;
    return true;
}

// AES-GCM needs a unique 12-byte nonce for every packet encrypted with the same key.
// The random nonce fields provide most of it, and sequence_num makes the nonce stable
// for this packet while still changing monotonically across the recording.
void packet_nonce(const file_packet_t& packet, uint8_t nonce[kGcmNonceSize])
{
    store_u32_be(nonce, packet.nonce_1);
    store_u32_be(nonce + 4, packet.nonce_2);
    store_u32_be(nonce + 8, packet.sequence_num);
}

bool encrypt_file_packet_locked(const file_packet_t& packet, encrypted_file_packet_t& encrypted)
{
    if (!g_packet_gcm_ready)
    {
        return false;
    }

    packet_nonce(packet, encrypted.nonce);
    return mbedtls_gcm_crypt_and_tag(&g_packet_gcm,
                                     MBEDTLS_GCM_ENCRYPT,
                                     sizeof(packet),
                                     encrypted.nonce,
                                     sizeof(encrypted.nonce),
                                     nullptr,
                                     0,
                                     reinterpret_cast<const uint8_t*>(&packet),
                                     encrypted.ciphertext,
                                     sizeof(encrypted.tag),
                                     encrypted.tag) == 0;
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
        g_longwrite = true; // this is to be set false at the top of every pump
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
        g_write_duration_average_us += (static_cast<float>(duration_us) - g_write_duration_average_us) * kWriteDurationAverageAlpha;
    }

    if (duration_us > g_write_duration_max_us)
    {
        g_write_duration_max_us = duration_us;
    }
}

void flush_if_due()
{
    const uint32_t now_ms = millis();
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
    return g_recording && g_file;
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
    g_recording             = false;
    return true;
}

bool write_packet(AudioFifo& fifo, filepkt_src_e source)
{
    std::unique_lock<std::mutex> lock(g_recorder_mutex);
    if (!recording_file_ready_locked())
    {
        return false;
    }

    const size_t available = fifo.availableToRead();
    if (available == 0)
    {
        // nothing to do
        return false;
    }

    uint32_t now_ms       = millis();
    uint32_t started_us   = micros();

    // create the packet to be written, starting with the header
    file_packet_t packet = {};
    packet.magic         = FILE_PACKET_HEADER_MAGIC;
    packet.src           = source;
    packet.flags         = flags_for_fifo(fifo); // error flags
    packet.ms_timestamp  = now_ms;
    packet.sequence_num  = g_sequence_num++;
    packet.fifo_cnt      = static_cast<uint32_t>(available);

    #if BUILD_WITH_SECURITY_LEVEL >= 1
    packet.nonce_1 = esp_random();
    packet.nonce_2 = esp_random();
    #endif

    // FILE_PACKET_PAYLOAD_MAX needs to be be about the same size as each fragment expected from the audio interface callbacks
    // otherwise we can adjust the FIFO watermark, or kPumpTargetFillPercentage
    // we don't want to under-utilize the packet payload buffer

    // read an appropriate amount from the FIFO
    const size_t samples_to_read = available < FILE_PACKET_PAYLOAD_MAX ? available : FILE_PACKET_PAYLOAD_MAX;
    int16_t      samples[FILE_PACKET_PAYLOAD_MAX];
    const size_t samples_read = fifo.dequeueMonoImmediate(samples, samples_to_read);
    if (samples_read == 0)
    {
        // hmmmm... should never happen
        return false;
    }

    // write the whole thing to the file
    packet.payload_length = static_cast<uint16_t>(samples_read);
    memcpy(packet.payload, samples, samples_read * sizeof(samples[0]));

    const uint8_t* data_ptr = g_pure_pcm_mode ? reinterpret_cast<const uint8_t*>(packet.payload) : reinterpret_cast<const uint8_t*>(&packet);
    size_t         data_sz  = g_pure_pcm_mode ? samples_read * sizeof(samples[0]) : sizeof(packet);

    #if BUILD_WITH_SECURITY_LEVEL >= 1
    encrypted_file_packet_t encrypted_packet = {};
    if (!g_pure_pcm_mode) // g_pure_pcm_mode is only for testing
    {
        if (!encrypt_file_packet_locked(packet, encrypted_packet))
        {
            DBG_LOGE(TAG, "packet encryption failed");
            g_recording = false;
            lock.unlock();
            show_fatal_error_f(true, "recording encryption failed");
            return false;
        }

        data_ptr = reinterpret_cast<const uint8_t*>(&encrypted_packet);
        data_sz  = sizeof(encrypted_packet);
    }
    #endif

    if (g_file.write(data_ptr, data_sz) != data_sz)
    {
        DBG_LOGE(TAG, "packet write failed");
        const bool fatal_card_failure = note_write_failure_locked();
        lock.unlock();
        if (fatal_card_failure)
        {
            fatal_recording_storage_failure("writing recording packet", "microSD recording write failed");
        }
        return false;
    }
    note_write_success_locked();

    // assume automatic cache flushing will happen when required, do not call flush manually

    g_bytes_written += data_sz;
    reset_fifo_flags(fifo);

    update_write_duration_stats(static_cast<uint32_t>(micros() - started_us));

    return true;
}

bool write_queued_meta_text_locked()
{
    if (!g_meta_text_queued)
    {
        return true;
    }

    if (g_pure_pcm_mode)
    {
        DBG_LOGW(TAG, "queued metadata text ignored in pure PCM mode");
        return true;
    }

    uint32_t started_us = micros();

    file_packet_t packet = {};
    packet.magic         = FILE_PACKET_HEADER_MAGIC;
    packet.src           = AUDSRC_META_TEXT;
    packet.flags         = 0;
    packet.ms_timestamp  = millis();
    packet.sequence_num  = g_sequence_num++;
    packet.fifo_cnt      = 0;
    packet.payload_length = static_cast<uint16_t>(g_queued_meta_text_length);
    memcpy(reinterpret_cast<uint8_t*>(packet.payload), g_queued_meta_text, g_queued_meta_text_length);

    #if BUILD_WITH_SECURITY_LEVEL >= 1
    packet.nonce_1 = esp_random();
    packet.nonce_2 = esp_random();
    encrypted_file_packet_t encrypted_packet = {};
    if (!encrypt_file_packet_locked(packet, encrypted_packet))
    {
        DBG_LOGE(TAG, "metadata text packet encryption failed");
        return false;
    }
    const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(&encrypted_packet);
    const size_t   data_sz  = sizeof(encrypted_packet);
    #else
    const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(&packet);
    const size_t   data_sz  = sizeof(packet);
    #endif

    if (g_file.write(data_ptr, data_sz) != data_sz)
    {
        DBG_LOGE(TAG, "metadata text packet write failed");
        return false;
    }

    g_bytes_written += data_sz;
    update_write_duration_stats(static_cast<uint32_t>(micros() - started_us));

    g_queued_meta_text[0]      = '\0';
    g_queued_meta_text_length  = 0;
    g_meta_text_queued         = false;
    return true;
}

bool pump_one_packet_locked()
{
    if (!g_host_fifo || !g_mic_fifo || !recording_file_ready())
    {
        return false;
    }

    AudioFifo*    first_fifo    = g_next_source_toggle ? g_host_fifo : g_mic_fifo;
    AudioFifo*    second_fifo   = g_next_source_toggle ? g_mic_fifo  : g_host_fifo;
    const filepkt_src_e first_source  = g_next_source_toggle ? AUDSRC_BT_16KHZ_MONO  : AUDSRC_MIC_16KHZ_MONO;
    const filepkt_src_e second_source = g_next_source_toggle ? AUDSRC_MIC_16KHZ_MONO : AUDSRC_BT_16KHZ_MONO;

    if (write_packet(*first_fifo, first_source))
    {
        g_next_source_toggle = !g_next_source_toggle;
        return true;
    }

    if (write_packet(*second_fifo, second_source))
    {
        return true;
    }

    return false;
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

    return g_host_fifo->getFillPercentage() <= kPumpTargetFillPercentage && g_mic_fifo->getFillPercentage() <= kPumpTargetFillPercentage;
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

} // private namespace

// public

bool init(AudioFifo& hostFifo, AudioFifo& micFifo)
{
    {
        std::lock_guard<std::mutex> lock(g_recorder_mutex);
        g_host_fifo = &hostFifo;
        g_mic_fifo  = &micFifo;
    }
    return true;
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

    g_host_fifo->setQueueEnabled(false);
    g_mic_fifo ->setQueueEnabled(false);
    g_host_fifo->clear();
    g_mic_fifo ->clear();

    char started_path[sizeof(g_sd_path)] = {};
    bool recording_file_opened = false;
    bool encryption_setup_failed = false;
    {
        std::lock_guard<std::mutex> pump_lock(g_pump_mutex);
        g_next_source_toggle = true;
    }
    {
        std::lock_guard<std::mutex> lock(g_recorder_mutex);

        // make the new file name and open it
        make_recording_path(typeCode);
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
                g_sd_path[0] = '\0';
                encryption_setup_failed = true;
            }
#endif

            if (!encryption_setup_failed)
            {
#ifdef ENABLE_FILE_PREALLOCATION
                // preallocate file space to avoid unexpected latency for editing file table entry
                grow_file(g_file, max_prealloc_size());
#endif

                g_bytes_written  = 0;
                g_sequence_num   = 0;
                g_last_flush_ms  = millis();
                g_consecutive_write_failures = 0;
                g_card_failure_reported      = false;
                g_recording_type_code        = typeCode;
                memo_type_from_code(typeCode, g_memo_type);
                clear_queued_meta_text_locked();
                g_recording      = true;
                recording_file_opened = true;
                strncpy(started_path, g_sd_path, sizeof(started_path) - 1);
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
        fatal_recording_storage_failure("opening recording file", "microSD recording file open failed");
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

void pump()
{
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

    g_longwrite = false;
    const uint32_t started = millis();
    // pump until no data or until too much time has passed
    do
    {
        if (!pump_one_packet_locked())
        {
            return;
        }
    } while (!fifos_below_pump_target() && (millis() - started) < kPumpBudgetMs);
}

bool stopRecording(bool estop)
{
    set_queue_enabled(false); // tells the queues to stop recording

    {
        std::lock_guard<std::mutex> lock(g_recorder_mutex);
        if (!g_recording && !g_file)
        {
            return true;
        }
    }

    bool     ok = true;
    char     stopped_path[sizeof(g_sd_path)] = {};
    uint64_t stopped_bytes = 0;
    bool     closed_file = false;
    std::lock_guard<std::mutex> pump_lock(g_pump_mutex);
    if (!estop)
    {
        // drain the last bit of the FIFO out, there is less pressure now since the FIFOs have been signalled to stop queuing
        while (pump_one_packet_locked())
        {
            taskYIELD();
        }
    }

    char     stopped_type_code = static_cast<char>(RecordingType::Unknown);
    MemoType stopped_memo_type = MEMO_TYPE_NOTE;
    {
        std::lock_guard<std::mutex> lock(g_recorder_mutex);
        if (!g_recording && !g_file)
        {
            return true;
        }

        stopped_type_code = g_recording_type_code;
        stopped_memo_type = g_memo_type;
    }
    prepare_stop_meta_text(stopped_type_code, stopped_memo_type);

    {
        std::lock_guard<std::mutex> lock(g_recorder_mutex);
        if (!g_recording && !g_file)
        {
            return true;
        }

        if (!write_queued_meta_text_locked())
        {
            ok = false;
        }

        g_recording = false;
        stopped_bytes = g_bytes_written;
        strncpy(stopped_path, g_sd_path, sizeof(stopped_path) - 1);

        if (g_file)
        {
            g_file.flush(); // this makes sure the buffer actually makes it onto the card
            g_last_flush_ms = millis();

            // shrink the grown file to what is required
            if (!g_file.truncate(g_bytes_written))
            {
                ok = false;
            }

            g_file.close();
            closed_file = true;
        }

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
        DBG_LOGE(TAG, "truncate failed");
        return false;
    }

    DBG_LOGI(TAG, "recording stopped: %s bytes=%llu", stopped_path, static_cast<unsigned long long>(stopped_bytes));
    return true;
}

bool isRecording()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    return g_recording;
}

bool purePcmMode()
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    return g_pure_pcm_mode;
}

void setPurePcmMode(bool enabled)
{
    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    g_pure_pcm_mode = enabled;
}

bool queueMetaText(const char* text)
{
    if (!text)
    {
        return false;
    }

    const size_t text_length = strnlen(text, kMetaTextPayloadMaxBytes + 1);
    const bool   fits        = text_length <= kMetaTextPayloadMaxBytes;
    const size_t copy_length = fits ? text_length : kMetaTextPayloadMaxBytes;

    std::lock_guard<std::mutex> lock(g_recorder_mutex);
    memcpy(g_queued_meta_text, text, copy_length);
    g_queued_meta_text[copy_length] = '\0';
    g_queued_meta_text_length       = copy_length;
    g_meta_text_queued              = copy_length > 0;
    return fits;
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

const char* currentSdPath()
{
    return g_sd_path;
}

bool grow_file(FsFile& file, uint64_t size)
{
    // growing a file means making the file gigantic at creation
    // this is so that while writing, there's no need to stop the writing in order to update the file table (or whatever mechanism is tracking file size)
    // resulting in a significant performance increase for file writing, lower latency, less unexpected cache flushes
    // the file will be truncated when it is closed

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

} // namespace AudioFileRecorder
