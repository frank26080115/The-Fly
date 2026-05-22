#include "AudioFileRecorder.h"

#include <M5Unified.h>
#include <mutex>
#include <string.h>

#include "ClockAgent.h"
#include "DiskStats.h"
#include "MicroSdCard.h"
#include "dbg_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

AudioFifo* g_host_fifo = nullptr;
AudioFifo* g_mic_fifo  = nullptr;
FsFile     g_file;
char       g_sd_path[48]                            = {};
char       g_queued_meta_text[kMetaTextPayloadMaxBytes + 1] = {};
size_t     g_queued_meta_text_length                = 0;
bool       g_meta_text_queued                       = false;
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

    #ifdef BUILD_WITH_SECURITY
    // TODO: `packet.nonce_1` set with a secure RNG
    // TODO: `packet.nonce_2` set with a secure RNG
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
    const size_t   data_sz  = g_pure_pcm_mode ? samples_read * sizeof(samples[0]) : sizeof(packet);

    #ifdef BUILD_WITH_SECURITY
    if (!g_pure_pcm_mode) // g_pure_pcm_mode is only for testing
    {
        // TODO: encrypt the entire packet
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

    if (g_file.write(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)) != sizeof(packet))
    {
        DBG_LOGE(TAG, "metadata text packet write failed");
        return false;
    }

    g_bytes_written += sizeof(packet);
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
#ifdef ENABLE_FILE_PREALLOCATION
            // preallocate file space to avoid unexpected latency for editing file table entry
            grow_file(g_file, max_prealloc_size());
#endif

            g_bytes_written  = 0;
            g_sequence_num   = 0;
            g_last_flush_ms  = millis();
            g_consecutive_write_failures = 0;
            g_card_failure_reported      = false;
            g_recording      = true;
            recording_file_opened = true;
            strncpy(started_path, g_sd_path, sizeof(started_path) - 1);
        }
    }
    if (!recording_file_opened)
    {
        fatal_recording_storage_failure("opening recording file", "microSD recording file open failed");
        return false;
    }
    set_queue_enabled(true); // actually start recording
    DBG_LOGI(TAG, "recording started: %s", started_path);
    return true;
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
    }

    if (closed_file)
    {
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
