#include "AudioFileRecorder.h"

#include <M5Unified.h>
#include <string.h>

#include "ClockAgent.h"
#include "MicroSdCard.h"
#include "dbg_log.h"

namespace AudioFileRecorder
{
namespace // private
{

constexpr const char* TAG = "AudioFileRecorder";

constexpr uint32_t kPumpBudgetMs             = 20;
constexpr uint8_t  kPumpTargetFillPercentage = 10;

AudioFifo* g_host_fifo = nullptr;
AudioFifo* g_mic_fifo  = nullptr;
FsFile     g_file;
char       g_sd_path[48]    = {};
uint64_t   g_bytes_written  = 0;
uint32_t   g_sequence_num   = 0;
bool       g_recording      = false;

bool init_sd()
{
    return MicroSdCard::begin();
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
    snprintf(g_sd_path, sizeof(g_sd_path), "/%c-%04d-%02d-%02d-%02d-%02d-%02d-U.raw", type_code, now.date.year, now.date.month, now.date.date, now.time.hours, now.time.minutes, now.time.seconds);
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

bool write_packet(AudioFifo& fifo, uint8_t source)
{
    const size_t available = fifo.availableToRead();
    if (available == 0)
    {
        // nothing to do
        return false;
    }

    // create the packet to be written, starting with the header
    file_packet_t packet = {};
    packet.magic         = FILE_PACKET_HEADER_MAGIC;
    packet.src           = source;
    packet.flags         = flags_for_fifo(fifo); // error flags

#ifdef FILE_CONTAINS_DEBUG
    packet.ms_timestamp = millis();
    packet.sequence_num = g_sequence_num++;
    packet.fifo_cnt     = static_cast<uint32_t>(available);
#endif

    // FILE_PACKET_PAYLOAD_MAX needs to be be about the same size as each fragment expected from the audio interface callbacks
    // otherwise we can adjust the FIFO watermark, or kPumpTargetFillPercentage
    // we don't want to under-utilize the packet payload buffer

    // read an appropriate amount from the FIFO
    const size_t samples_to_read = available < FILE_PACKET_PAYLOAD_MAX ? available : FILE_PACKET_PAYLOAD_MAX;
    const size_t samples_read    = fifo.dequeueMonoImmediate(reinterpret_cast<int16_t*>(packet.payload), samples_to_read);
    if (samples_read == 0)
    {
        // hmmmm... should never happen
        return false;
    }

    // write the whole thing to the file
    packet.payload_length = static_cast<uint16_t>(samples_read);
    if (g_file.write(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)) != sizeof(packet))
    {
        DBG_LOGE(TAG, "packet write failed");
        return false;
    }

    // assume automatic cache flushing will happen when required, do not call flush manually

    g_bytes_written += sizeof(packet);
    reset_fifo_flags(fifo);
    return true;
}

bool pump_one_packet()
{
    static bool next_source_toggle = true; // this function call will only do one packet, so we have a toggle to pick which of the two FIFOs is the source

    if (!g_host_fifo || !g_mic_fifo || !g_file)
    {
        return false;
    }

    if (g_bytes_written == 0) {
        next_source_toggle = true;
    }

    AudioFifo*    first_fifo    = next_source_toggle ? g_host_fifo : g_mic_fifo;
    AudioFifo*    second_fifo   = next_source_toggle ? g_mic_fifo  : g_host_fifo;
    const uint8_t first_source  = next_source_toggle ? AUDSRC_BT   : AUDSRC_MIC;
    const uint8_t second_source = next_source_toggle ? AUDSRC_MIC  : AUDSRC_BT;

    if (write_packet(*first_fifo, first_source))
    {
        next_source_toggle = !next_source_toggle;
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
    g_host_fifo = &hostFifo;
    g_mic_fifo  = &micFifo;
    return init_sd();
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

    if (g_recording)
    {
        // if already recording, stop recording and start a new file
        stopRecording();
    }

    // make sure card is ready
    if (!init_sd())
    {
        return false;
    }

    g_host_fifo->setQueueEnabled(false);
    g_mic_fifo ->setQueueEnabled(false);
    g_host_fifo->clear();
    g_mic_fifo ->clear();

    // make the new file name and open it
    make_recording_path(typeCode);
    if (!g_file.open(g_sd_path, O_RDWR | O_CREAT | O_TRUNC))
    {
        DBG_LOGE(TAG, "open failed: %s", g_sd_path);
        return false;
    }

    // preallocate file space to avoid unexpected latency for editing file table entry
    grow_file(g_file, max_prealloc_size());

    g_bytes_written  = 0;
    g_sequence_num   = 0;
    g_recording      = true;
    set_queue_enabled(true); // actually start recording
    DBG_LOGI(TAG, "recording started: %s", g_sd_path);
    return true;
}

void pump()
{
    if (!g_recording || !g_file)
    {
        return;
    }

    const uint32_t started = millis();
    // pump until no data or until too much time has passed
    do
    {
        if (!pump_one_packet())
        {
            return;
        }
    } while (!fifos_below_pump_target() && (millis() - started) < kPumpBudgetMs);
}

bool stopRecording()
{
    set_queue_enabled(false); // tells the queues to stop recording

    if (!g_recording && !g_file)
    {
        return true;
    }

    // drain the last bit of the FIFO out, there is less pressure now since the FIFOs have been signalled to stop queuing
    while (pump_one_packet())
    {
        taskYIELD();
    }

    g_recording = false;

    if (g_file)
    {
        g_file.flush(); // this makes sure the buffer actually makes it onto the card

        // shrink the grown file to what is required
        if (!g_file.truncate(g_bytes_written))
        {
            DBG_LOGE(TAG, "truncate failed");
            g_file.close();
            return false;
        }

        g_file.close();
    }

    DBG_LOGI(TAG, "recording stopped: %s bytes=%llu", g_sd_path, static_cast<unsigned long long>(g_bytes_written));
    return true;
}

bool isRecording()
{
    return g_recording;
}

uint64_t bytesWritten()
{
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
