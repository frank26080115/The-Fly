#include "AudioFileRecorder.h"

#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "conf.h"
#include "defs.h"
#include "ClockAgent.h"
#include "esp_log.h"

namespace AudioFileRecorder
{
namespace
{

constexpr const char* TAG = "AudioFileRecorder";

constexpr int kCore2SdSclk = 18;
constexpr int kCore2SdMiso = 38;
constexpr int kCore2SdMosi = 23;
constexpr int kCore2SdCs   = 4;

constexpr uint32_t kSdFrequencyHz            = 40000000U;
constexpr uint32_t kPumpBudgetMs             = 20;
constexpr uint8_t  kPumpTargetFillPercentage = 10;

AudioFifo* g_bt_fifo  = nullptr;
AudioFifo* g_mic_fifo = nullptr;
File       g_file;
char       g_sd_path[48]    = {};
char       g_vfs_path[56]   = {};
uint64_t   g_bytes_written  = 0;
uint32_t   g_sequence_num   = 0;
bool       g_sd_ready       = false;
bool       g_recording      = false;
bool       g_next_source_bt = true;

bool init_sd()
{
    if (g_sd_ready)
    {
        return true;
    }

    SPI.begin(kCore2SdSclk, kCore2SdMiso, kCore2SdMosi, kCore2SdCs);
    if (!SD.begin(kCore2SdCs, SPI, kSdFrequencyHz))
    {
        ESP_LOGE(TAG, "microSD init failed");
        return false;
    }

    const uint64_t total = SD.totalBytes();
    const uint64_t used  = SD.usedBytes();
    ESP_LOGI(TAG, "microSD space: total=%llu used=%llu free=%llu", static_cast<unsigned long long>(total), static_cast<unsigned long long>(used), static_cast<unsigned long long>(total > used ? total - used : 0));
    g_sd_ready = true;
    return true;
}

uint64_t max_prealloc_size()
{
    const uint64_t total      = SD.totalBytes();
    const uint64_t used       = SD.usedBytes();
    const uint64_t free_bytes = total > used ? total - used : 0;
    uint64_t       size       = (free_bytes / kHalfGiB) * kHalfGiB;

    if (size > kMaxGrowFileBytes)
    {
        size = kMaxGrowFileBytes;
    }
    return size;
}

void make_recording_path(char type_code)
{
    m5::rtc_datetime_t now = Clock.getDateTime();
    snprintf(g_sd_path, sizeof(g_sd_path), "/%c-%04d-%02d-%02d-%02d-%02d-%02d-U.raw", type_code, now.date.year, now.date.month, now.date.date, now.time.hours, now.time.minutes, now.time.seconds);
    snprintf(g_vfs_path, sizeof(g_vfs_path), "/sd%s", g_sd_path);
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
        return false;
    }

    file_packet_t packet = {};
    packet.magic         = FILE_PACKET_HEADER_MAGIC;
    packet.src           = source;
    packet.flags         = flags_for_fifo(fifo);

#ifdef FILE_CONTAINS_DEBUG
    packet.ms_timestamp = millis();
    packet.sequence_num = g_sequence_num++;
    packet.fifo_cnt     = static_cast<uint32_t>(available);
#endif

    const size_t samples_to_read = available < FILE_PACKET_PAYLOAD_MAX ? available : FILE_PACKET_PAYLOAD_MAX;
    const size_t samples_read    = fifo.dequeueMonoImmediate(reinterpret_cast<int16_t*>(packet.payload), samples_to_read);
    if (samples_read == 0)
    {
        return false;
    }

    packet.payload_length = static_cast<uint16_t>(samples_read);
    if (g_file.write(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)) != sizeof(packet))
    {
        ESP_LOGE(TAG, "packet write failed");
        return false;
    }

    g_bytes_written += sizeof(packet);
    reset_fifo_flags(fifo);
    return true;
}

bool pump_one_packet()
{
    if (!g_bt_fifo || !g_mic_fifo || !g_file)
    {
        return false;
    }

    AudioFifo*    first_fifo    = g_next_source_bt ? g_bt_fifo : g_mic_fifo;
    AudioFifo*    second_fifo   = g_next_source_bt ? g_mic_fifo : g_bt_fifo;
    const uint8_t first_source  = g_next_source_bt ? AUDSRC_BT : AUDSRC_MIC;
    const uint8_t second_source = g_next_source_bt ? AUDSRC_MIC : AUDSRC_BT;

    if (write_packet(*first_fifo, first_source))
    {
        g_next_source_bt = !g_next_source_bt;
        return true;
    }

    if (write_packet(*second_fifo, second_source))
    {
        g_next_source_bt = !g_next_source_bt;
        return true;
    }

    return false;
}

bool fifos_below_pump_target()
{
    if (!g_bt_fifo || !g_mic_fifo)
    {
        return true;
    }

    return g_bt_fifo->getFillPercentage() <= kPumpTargetFillPercentage && g_mic_fifo->getFillPercentage() <= kPumpTargetFillPercentage;
}

void set_queue_enabled(bool enabled)
{
    if (g_bt_fifo)
    {
        g_bt_fifo->setQueueEnabled(enabled);
    }
    if (g_mic_fifo)
    {
        g_mic_fifo->setQueueEnabled(enabled);
    }
}

} // namespace

bool init(AudioFifo& btFifo, AudioFifo& micFifo)
{
    g_bt_fifo  = &btFifo;
    g_mic_fifo = &micFifo;
    return init_sd();
}

bool startRecording(RecordingType type)
{
    return startRecording(static_cast<char>(type));
}

bool startRecording(char typeCode)
{
    if (!g_bt_fifo || !g_mic_fifo)
    {
        ESP_LOGE(TAG, "not initialized");
        return false;
    }

    if (g_recording)
    {
        stopRecording();
    }

    if (!init_sd())
    {
        return false;
    }

    g_bt_fifo->setQueueEnabled(false);
    g_mic_fifo->setQueueEnabled(false);
    g_bt_fifo->clear();
    g_mic_fifo->clear();

    make_recording_path(typeCode);
    g_file = SD.open(g_sd_path, FILE_WRITE);
    if (!g_file)
    {
        ESP_LOGE(TAG, "open failed: %s", g_sd_path);
        return false;
    }

    grow_file(g_file, max_prealloc_size());

    g_bytes_written  = 0;
    g_sequence_num   = 0;
    g_next_source_bt = true;
    g_recording      = true;
    set_queue_enabled(true);
    ESP_LOGI(TAG, "recording started: %s", g_sd_path);
    return true;
}

void pump()
{
    if (!g_recording || !g_file)
    {
        return;
    }

    const uint32_t started = millis();
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
    if (!g_recording && !g_file)
    {
        set_queue_enabled(false);
        return true;
    }

    set_queue_enabled(false);

    while (pump_one_packet())
    {
        taskYIELD();
    }

    if (g_file)
    {
        g_file.flush();
        g_file.close();
    }

    g_recording = false;

    if (g_vfs_path[0] != '\0' && truncate(g_vfs_path, static_cast<off_t>(g_bytes_written)) != 0)
    {
        ESP_LOGE(TAG, "truncate failed: errno=%d", errno);
        return false;
    }

    ESP_LOGI(TAG, "recording stopped: %s bytes=%llu", g_sd_path, static_cast<unsigned long long>(g_bytes_written));
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

const char* currentVfsPath()
{
    return g_vfs_path;
}

bool grow_file(File& file, uint64_t size)
{
    if (size > kMaxGrowFileBytes)
    {
        size = kMaxGrowFileBytes;
    }

    while (size >= kHalfGiB)
    {
        if (file.seek(static_cast<uint32_t>(size - 1)))
        {
            if (file.write(static_cast<uint8_t>(0)) == 1)
            {
                file.flush();
                file.seek(0);
                ESP_LOGI(TAG, "reserved %llu bytes", static_cast<unsigned long long>(size));
                return true;
            }
        }

        ESP_LOGW(TAG, "reserve failed at %llu bytes", static_cast<unsigned long long>(size));
        size -= kHalfGiB;
    }

    file.seek(0);
    return false;
}

} // namespace AudioFileRecorder
