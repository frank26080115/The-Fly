#include "AudioFileDecoder.h"

#include <Arduino.h>
#include <SdFat.h>
#include <algorithm>
#include <new>
#include <stdio.h>
#include <string.h>

#include "AudioFifo.h"
#include "MicroSdCard.h"
#include "dbg_log.h"

#if BUILD_WITH_SECURITY_LEVEL >= 1
#include "Aegis.h"
#include "mbedtls/gcm.h"
#endif

namespace AudioFileRecorder
{
namespace
{

constexpr const char* TAG = "AudioFileDecoder";

constexpr size_t   kFifoCapacitySamples   = 2048;
constexpr size_t   kFifoWatermarkSamples  = 512;
constexpr size_t   kDequeueFrames         = 128;
constexpr uint32_t kTaskStackBytes        = 4096;
constexpr uint32_t kDestroyPollMs         = 10;
constexpr uint32_t kProgressIntervalMs    = 500;
constexpr uint32_t kProgressPacketCadence = 8;
constexpr uint32_t kOutputSampleRateHz    = 16000;
constexpr uint16_t kOutputChannels        = 2;
constexpr uint16_t kBitsPerSample         = 16;
constexpr size_t   kWavHeaderSize         = 44;

#if BUILD_WITH_SECURITY_LEVEL >= 1
constexpr size_t kGcmNonceSize = 12;
constexpr size_t kGcmTagSize   = 16;
constexpr size_t kEncryptedPacketSize = kGcmNonceSize + sizeof(file_packet_t) + kGcmTagSize;
#endif

struct DecoderTaskContext
{
    char   input_path[AudioFileDecoder::kPathMaxLength] = {};
    char   output_path[AudioFileDecoder::kPathMaxLength] = {};
    char   temp_path[AudioFileDecoder::kPathMaxLength] = {};
    FsFile input;
    FsFile output;
    bool   output_opened = false;
    bool   temp_created = false;

#if BUILD_WITH_SECURITY_LEVEL >= 1
    mbedtls_gcm_context gcm;
    bool                gcm_initialized = false;
#endif
};

const char* state_name(AudioFileDecoder::State state)
{
    switch (state)
    {
    case AudioFileDecoder::State::Idle:
        return "Idle";
    case AudioFileDecoder::State::Busy:
        return "Busy";
    case AudioFileDecoder::State::Done:
        return "Done";
    case AudioFileDecoder::State::Error:
        return "Error";
    case AudioFileDecoder::State::Cancelled:
        return "Cancelled";
    default:
        return "Unknown";
    }
}

const char* error_name(AudioFileDecoder::Error error)
{
    switch (error)
    {
    case AudioFileDecoder::Error::None:
        return "None";
    case AudioFileDecoder::Error::AlreadyBusy:
        return "AlreadyBusy";
    case AudioFileDecoder::Error::InvalidArgument:
        return "InvalidArgument";
    case AudioFileDecoder::Error::SdNotReady:
        return "SdNotReady";
    case AudioFileDecoder::Error::TaskCreateFailed:
        return "TaskCreateFailed";
    case AudioFileDecoder::Error::AllocationFailed:
        return "AllocationFailed";
    case AudioFileDecoder::Error::InputOpenFailed:
        return "InputOpenFailed";
    case AudioFileDecoder::Error::InputReadFailed:
        return "InputReadFailed";
    case AudioFileDecoder::Error::OutputOpenFailed:
        return "OutputOpenFailed";
    case AudioFileDecoder::Error::OutputWriteFailed:
        return "OutputWriteFailed";
    case AudioFileDecoder::Error::OutputSeekFailed:
        return "OutputSeekFailed";
    case AudioFileDecoder::Error::OutputRenameFailed:
        return "OutputRenameFailed";
    case AudioFileDecoder::Error::InvalidPacket:
        return "InvalidPacket";
    case AudioFileDecoder::Error::EncryptionSetupFailed:
        return "EncryptionSetupFailed";
    case AudioFileDecoder::Error::DecryptionFailed:
        return "DecryptionFailed";
    case AudioFileDecoder::Error::Cancelled:
        return "Cancelled";
    default:
        return "Unknown";
    }
}

bool copy_text(char* dst, size_t dst_size, const char* src)
{
    if (!dst || dst_size == 0)
    {
        return false;
    }

    const char*  value = src ? src : "";
    const size_t length = strlen(value);
    if (length >= dst_size)
    {
        dst[0] = '\0';
        return false;
    }

    memcpy(dst, value, length + 1);
    return true;
}

const char* last_path_separator(const char* path)
{
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    if (!slash)
    {
        return backslash;
    }
    if (!backslash)
    {
        return slash;
    }
    return slash > backslash ? slash : backslash;
}

bool make_output_path(const char* input_path, char* output_path, size_t output_path_size)
{
    if (!copy_text(output_path, output_path_size, input_path))
    {
        return false;
    }

    const char* separator = last_path_separator(output_path);
    char*       basename = const_cast<char*>(separator ? separator + 1 : output_path);
    char*       dot = strrchr(basename, '.');
    if (dot)
    {
        if (static_cast<size_t>(dot - output_path) + 4 >= output_path_size)
        {
            output_path[0] = '\0';
            return false;
        }
        memcpy(dot, ".wav", 5);
        return true;
    }

    const size_t length = strlen(output_path);
    if (length + 4 >= output_path_size)
    {
        output_path[0] = '\0';
        return false;
    }

    memcpy(output_path + length, ".wav", 5);
    return true;
}

bool make_temp_path(const char* input_path, char* temp_path, size_t temp_path_size)
{
    if (!input_path || !temp_path || temp_path_size == 0)
    {
        return false;
    }

    const char* separator = last_path_separator(input_path);
    if (!separator)
    {
        return copy_text(temp_path, temp_path_size, "temp.wav");
    }

    const size_t parent_length = static_cast<size_t>(separator - input_path) + 1;
    if (parent_length + strlen("temp.wav") >= temp_path_size)
    {
        temp_path[0] = '\0';
        return false;
    }

    memcpy(temp_path, input_path, parent_length);
    memcpy(temp_path + parent_length, "temp.wav", strlen("temp.wav") + 1);
    return true;
}

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

bool write_wav_header(FsFile& output, uint32_t data_bytes)
{
    uint8_t header[kWavHeaderSize] = {};

    memcpy(header + 0, "RIFF", 4);
    write_le32(header + 4, data_bytes + 36);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    write_le32(header + 16, 16);
    write_le16(header + 20, 1);
    write_le16(header + 22, kOutputChannels);
    write_le32(header + 24, kOutputSampleRateHz);
    write_le32(header + 28, kOutputSampleRateHz * kOutputChannels * (kBitsPerSample / 8));
    write_le16(header + 32, kOutputChannels * (kBitsPerSample / 8));
    write_le16(header + 34, kBitsPerSample);
    memcpy(header + 36, "data", 4);
    write_le32(header + 40, data_bytes);

    return output.write(header, sizeof(header)) == sizeof(header);
}

bool packet_header_valid(const file_packet_t& packet)
{
    if (packet.magic != FILE_PACKET_HEADER_MAGIC)
    {
        return false;
    }

    if (packet.src == AUDSRC_META_TEXT)
    {
        return packet.payload_length <= FILE_PACKET_PAYLOAD_MAX * sizeof(uint16_t);
    }

    return packet.payload_length <= FILE_PACKET_PAYLOAD_MAX;
}

float progress_percent(uint64_t processed, uint64_t total)
{
    if (total == 0)
    {
        return 0.0f;
    }

    if (processed >= total)
    {
        return 100.0f;
    }

    return static_cast<float>((static_cast<double>(processed) * 100.0) / static_cast<double>(total));
}

#if BUILD_WITH_SECURITY_LEVEL >= 1
bool setup_packet_decryption(mbedtls_gcm_context& gcm)
{
    if (!Aegis::isInitialized() && !Aegis::init())
    {
        return false;
    }

    const uint8_t* filecrypt_key = Aegis::getFilecryptKey();
    if (!filecrypt_key)
    {
        return false;
    }

    return mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, filecrypt_key, Aegis::kFilecryptKeySize * 8) == 0;
}

bool decrypt_packet(mbedtls_gcm_context& gcm, const uint8_t* encrypted, file_packet_t& packet)
{
    if (!encrypted)
    {
        return false;
    }

    const uint8_t* nonce = encrypted;
    const uint8_t* ciphertext = encrypted + kGcmNonceSize;
    const uint8_t* tag = ciphertext + sizeof(file_packet_t);

    return mbedtls_gcm_auth_decrypt(&gcm,
                                    sizeof(file_packet_t),
                                    nonce,
                                    kGcmNonceSize,
                                    nullptr,
                                    0,
                                    tag,
                                    kGcmTagSize,
                                    ciphertext,
                                    reinterpret_cast<uint8_t*>(&packet)) == 0;
}
#endif

void init_context_security(DecoderTaskContext& context)
{
#if BUILD_WITH_SECURITY_LEVEL >= 1
    mbedtls_gcm_init(&context.gcm);
    context.gcm_initialized = true;
#else
    (void)context;
#endif
}

void free_context_security(DecoderTaskContext& context)
{
#if BUILD_WITH_SECURITY_LEVEL >= 1
    if (context.gcm_initialized)
    {
        mbedtls_gcm_free(&context.gcm);
        context.gcm_initialized = false;
    }
#else
    (void)context;
#endif
}

void cleanup_task_context(SdFs& fs, DecoderTaskContext*& context, bool remove_temp)
{
    if (!context)
    {
        return;
    }

    if (context->input)
    {
        context->input.close();
    }
    if (context->output_opened)
    {
        context->output.close();
        context->output_opened = false;
    }
    if (remove_temp && context->temp_created)
    {
        fs.remove(context->temp_path);
        context->temp_created = false;
    }

    free_context_security(*context);
    delete context;
    context = nullptr;
}

} // namespace

AudioFileDecoder::AudioFileDecoder(const char* inputPath,
                                   CompleteCallback onComplete,
                                   ProgressCallback onProgress)
    : m_on_complete(onComplete),
      m_on_progress(onProgress)
{
    copy_text(m_status.input_path, sizeof(m_status.input_path), inputPath);
    make_output_path(m_status.input_path, m_status.output_path, sizeof(m_status.output_path));
}

AudioFileDecoder::~AudioFileDecoder()
{
    cancel();

    while (true)
    {
        TaskHandle_t task_handle = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            task_handle = m_task_handle;
        }

        if (!task_handle || task_handle == xTaskGetCurrentTaskHandle())
        {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(kDestroyPollMs));
    }

    releaseWorkingBuffers();
}

bool AudioFileDecoder::start()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_status.state == State::Busy)
        {
            m_status.error = Error::AlreadyBusy;
            return false;
        }
    }

    if (m_status.input_path[0] == '\0' || m_status.output_path[0] == '\0')
    {
        setFinishedWithoutTask(State::Error, Error::InvalidArgument);
        return false;
    }

    releaseWorkingBuffers();
    if (!allocateWorkingBuffers())
    {
        releaseWorkingBuffers();
        setFinishedWithoutTask(State::Error, Error::AllocationFailed);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.bytes_processed = 0;
        m_status.bytes_total = 0;
        m_status.progress = 0.0f;
        m_status.state = State::Busy;
        m_status.error = Error::None;
        m_status.finished = false;
        m_complete_callback_fired = false;
        m_progress_pending = false;
        m_cancel_requested.store(false, std::memory_order_relaxed);
    }

    if (!startTaskOnCurrentCore())
    {
        releaseWorkingBuffers();
        setFinishedWithoutTask(State::Error, Error::TaskCreateFailed);
        return false;
    }

    return true;
}

bool AudioFileDecoder::cancel()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const bool busy = m_status.state == State::Busy && m_task_handle != nullptr;
    if (busy)
    {
        m_cancel_requested.store(true, std::memory_order_relaxed);
    }
    return busy;
}

void AudioFileDecoder::poll()
{
    CompleteCallback complete_callback = nullptr;
    ProgressCallback progress_callback = nullptr;
    Status           complete_status = {};
    Status           progress_status = {};
    bool             fire_complete = false;
    bool             fire_progress = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_progress_pending)
        {
            m_progress_pending = false;
            progress_callback = m_on_progress;
            progress_status = m_status;
            fire_progress = progress_callback != nullptr;
        }

        if (m_status.finished && !m_complete_callback_fired)
        {
            m_complete_callback_fired = true;
            complete_callback = m_on_complete;
            complete_status = m_status;
            fire_complete = complete_callback != nullptr;
        }
    }

    if (fire_progress)
    {
        progress_callback(progress_status);
    }

    if (fire_complete)
    {
        complete_callback(complete_status);
    }
}

AudioFileDecoder::State AudioFileDecoder::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status.state;
}

AudioFileDecoder::Error AudioFileDecoder::error() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status.error;
}

AudioFileDecoder::Status AudioFileDecoder::status() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

float AudioFileDecoder::progress() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status.progress;
}

void AudioFileDecoder::setOnCompleteCallback(CompleteCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_complete = callback;
}

void AudioFileDecoder::setOnProgressCallback(ProgressCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_progress = callback;
}

const char* AudioFileDecoder::stateName() const
{
    return state_name(state());
}

const char* AudioFileDecoder::errorName() const
{
    return error_name(error());
}

void AudioFileDecoder::taskEntry(void* argument)
{
    AudioFileDecoder* decoder = static_cast<AudioFileDecoder*>(argument);
    if (decoder)
    {
        decoder->taskMain();
    }
    vTaskDelete(nullptr);
}

bool AudioFileDecoder::allocateWorkingBuffers()
{
    m_left_fifo.reset(new (std::nothrow) AudioFifo(kFifoCapacitySamples, kFifoWatermarkSamples));
    m_right_fifo.reset(new (std::nothrow) AudioFifo(kFifoCapacitySamples, kFifoWatermarkSamples));
    m_packet.reset(new (std::nothrow) file_packet_t);
    m_packet_samples.reset(new (std::nothrow) int16_t[FILE_PACKET_PAYLOAD_MAX]);
    m_left_samples.reset(new (std::nothrow) int16_t[kDequeueFrames]);
    m_right_samples.reset(new (std::nothrow) int16_t[kDequeueFrames]);
    m_stereo_samples.reset(new (std::nothrow) int16_t[kDequeueFrames * kOutputChannels]);

#if BUILD_WITH_SECURITY_LEVEL >= 1
    m_encrypted_packet.reset(new (std::nothrow) uint8_t[kEncryptedPacketSize]);
#else
    m_encrypted_packet.reset();
#endif

    if (!m_left_fifo || !m_right_fifo || !m_packet || !m_packet_samples || !m_left_samples || !m_right_samples || !m_stereo_samples)
    {
        return false;
    }

#if BUILD_WITH_SECURITY_LEVEL >= 1
    if (!m_encrypted_packet)
    {
        return false;
    }
#endif

    return m_left_fifo->begin() && m_right_fifo->begin();
}

void AudioFileDecoder::releaseWorkingBuffers()
{
    if (m_left_fifo)
    {
        m_left_fifo->end();
    }
    if (m_right_fifo)
    {
        m_right_fifo->end();
    }

    m_left_fifo.reset();
    m_right_fifo.reset();
    m_packet.reset();
    m_encrypted_packet.reset();
    m_packet_samples.reset();
    m_left_samples.reset();
    m_right_samples.reset();
    m_stereo_samples.reset();
}

bool AudioFileDecoder::startTaskOnCurrentCore()
{
    TaskHandle_t task_handle = nullptr;
    const BaseType_t created = xTaskCreatePinnedToCore(taskEntry,
                                                       "AudioFileDecoder",
                                                       kTaskStackBytes,
                                                       this,
                                                       1,
                                                       &task_handle,
                                                       xPortGetCoreID());
    if (created != pdPASS)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_status.state == State::Busy && !m_status.finished)
    {
        m_task_handle = task_handle;
    }
    return true;
}

void AudioFileDecoder::updateProgress(uint64_t bytesProcessed, uint64_t bytesTotal, bool notify)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.bytes_processed = bytesProcessed;
    m_status.bytes_total = bytesTotal;
    m_status.progress = progress_percent(bytesProcessed, bytesTotal);
    if (notify)
    {
        m_progress_pending = true;
    }
}

bool AudioFileDecoder::cancelRequested() const
{
    return m_cancel_requested.load(std::memory_order_relaxed);
}

void AudioFileDecoder::finish(State state, Error error)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.state = state;
        m_status.error = error;
        m_status.finished = true;
        m_task_handle = nullptr;
        m_progress_pending = true;
        m_cancel_requested.store(false, std::memory_order_relaxed);
    }

    if (state == State::Error)
    {
        DBG_LOGE(TAG, "decode failed: %s", error_name(error));
    }
    else if (state == State::Cancelled)
    {
        DBG_LOGW(TAG, "decode cancelled");
    }
    else
    {
        DBG_LOGI(TAG, "decode complete");
    }
}

void AudioFileDecoder::setFinishedWithoutTask(State state, Error error)
{
    finish(state, error);
}

void AudioFileDecoder::taskMain()
{
    if (!MicroSdCard::isReady())
    {
        releaseWorkingBuffers();
        finish(State::Error, Error::SdNotReady);
        return;
    }

    SdFs& fs = MicroSdCard::fs();
    DecoderTaskContext* context = new (std::nothrow) DecoderTaskContext;
    if (!context)
    {
        releaseWorkingBuffers();
        finish(State::Error, Error::AllocationFailed);
        return;
    }
    init_context_security(*context);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        copy_text(context->input_path, sizeof(context->input_path), m_status.input_path);
        copy_text(context->output_path, sizeof(context->output_path), m_status.output_path);
    }

    if (!make_temp_path(context->input_path, context->temp_path, sizeof(context->temp_path)) ||
        strcmp(context->temp_path, context->output_path) == 0)
    {
        cleanup_task_context(fs, context, false);
        releaseWorkingBuffers();
        finish(State::Error, Error::InvalidArgument);
        return;
    }

#if BUILD_WITH_SECURITY_LEVEL >= 1
    if (!setup_packet_decryption(context->gcm))
    {
        cleanup_task_context(fs, context, false);
        releaseWorkingBuffers();
        finish(State::Error, Error::EncryptionSetupFailed);
        return;
    }
#endif

    if (!context->input.open(context->input_path, O_RDONLY))
    {
        cleanup_task_context(fs, context, false);
        releaseWorkingBuffers();
        finish(State::Error, Error::InputOpenFailed);
        return;
    }

    const uint64_t input_size = context->input.fileSize();
    updateProgress(0, input_size, true);

    if (!context->output.open(context->temp_path, O_RDWR | O_CREAT | O_TRUNC))
    {
        cleanup_task_context(fs, context, false);
        releaseWorkingBuffers();
        finish(State::Error, Error::OutputOpenFailed);
        return;
    }
    context->output_opened = true;
    context->temp_created = true;

    if (!write_wav_header(context->output, 0))
    {
        cleanup_task_context(fs, context, true);
        releaseWorkingBuffers();
        finish(State::Error, Error::OutputWriteFailed);
        return;
    }

    bool     eof = false;
    uint32_t data_bytes = 0;
    uint32_t packets_decoded = 0;
    uint32_t last_progress_ms = millis();
    Error    failure_error = Error::None;

    while (true)
    {
        if (cancelRequested())
        {
            cleanup_task_context(fs, context, true);
            releaseWorkingBuffers();
            finish(State::Cancelled, Error::Cancelled);
            return;
        }

        bool did_work = false;

        if (!eof &&
            m_left_fifo->availableToWrite() >= kFifoWatermarkSamples &&
            m_right_fifo->availableToWrite() >= kFifoWatermarkSamples)
        {
#if BUILD_WITH_SECURITY_LEVEL >= 1
            const int bytes_read = context->input.read(m_encrypted_packet.get(), kEncryptedPacketSize);
            if (bytes_read == 0)
            {
                eof = true;
                m_left_fifo->setWatermark(0);
                m_right_fifo->setWatermark(0);
            }
            else if (bytes_read != static_cast<int>(kEncryptedPacketSize))
            {
                failure_error = Error::InputReadFailed;
                break;
            }
            else if (!decrypt_packet(context->gcm, m_encrypted_packet.get(), *m_packet))
            {
                failure_error = Error::DecryptionFailed;
                break;
            }
#else
            const int bytes_read = context->input.read(reinterpret_cast<uint8_t*>(m_packet.get()), sizeof(file_packet_t));
            if (bytes_read == 0)
            {
                eof = true;
                m_left_fifo->setWatermark(0);
                m_right_fifo->setWatermark(0);
            }
            else if (bytes_read != static_cast<int>(sizeof(file_packet_t)))
            {
                failure_error = Error::InputReadFailed;
                break;
            }
#endif

            if (!eof)
            {
                if (!packet_header_valid(*m_packet))
                {
                    failure_error = Error::InvalidPacket;
                    break;
                }

                if (m_packet->src != AUDSRC_META_TEXT)
                {
                    AudioFifo& target_fifo = (static_cast<uint8_t>(m_packet->src) & 1U) ? *m_left_fifo : *m_right_fifo;
                    const size_t samples_to_queue = std::min<size_t>(m_packet->payload_length, FILE_PACKET_PAYLOAD_MAX);
                    memcpy(m_packet_samples.get(), m_packet->payload, samples_to_queue * sizeof(int16_t));
                    target_fifo.queue(m_packet_samples.get(), samples_to_queue);
                }

                ++packets_decoded;
                if ((packets_decoded % kProgressPacketCadence) == 0)
                {
                    const uint32_t now = millis();
                    if (now - last_progress_ms >= kProgressIntervalMs)
                    {
                        updateProgress(context->input.curPosition(), input_size, true);
                        last_progress_ms = now;
                    }
                }
            }

            did_work = true;
        }

        const bool left_ready_initial = m_left_fifo->readyToDequeue();
        const bool right_ready_initial = m_right_fifo->readyToDequeue();
        const bool drain_until_empty = left_ready_initial && right_ready_initial;
        while (left_ready_initial || right_ready_initial)
        {
            const bool left_ready = m_left_fifo->readyToDequeue();
            const bool right_ready = m_right_fifo->readyToDequeue();
            if (!left_ready && !right_ready)
            {
                break;
            }

            size_t left_count = 0;
            size_t right_count = 0;

            if (left_ready)
            {
                left_count = m_left_fifo->dequeueMono(m_left_samples.get(), kDequeueFrames);
            }
            if (right_ready)
            {
                right_count = m_right_fifo->dequeueMono(m_right_samples.get(), kDequeueFrames);
            }

            const size_t frames = std::max(left_count, right_count);
            if (frames == 0)
            {
                break;
            }

            if (frames > 0)
            {
                for (size_t i = 0; i < frames; ++i)
                {
                    m_stereo_samples[i * 2] = i < left_count ? m_left_samples[i] : 0;
                    m_stereo_samples[i * 2 + 1] = i < right_count ? m_right_samples[i] : 0;
                }

                const size_t bytes_to_write = frames * kOutputChannels * sizeof(int16_t);
                if (context->output.write(reinterpret_cast<const uint8_t*>(m_stereo_samples.get()), bytes_to_write) != bytes_to_write)
                {
                    failure_error = Error::OutputWriteFailed;
                    break;
                }
                data_bytes += static_cast<uint32_t>(bytes_to_write);
                did_work = true;
            }

            if (failure_error != Error::None || !drain_until_empty)
            {
                break;
            }
        }

        if (eof && !m_left_fifo->readyToDequeue() && !m_right_fifo->readyToDequeue())
        {
            break;
        }

        if (!did_work)
        {
            taskYIELD();
        }
    }

    if (failure_error != Error::None)
    {
        cleanup_task_context(fs, context, true);
        releaseWorkingBuffers();
        finish(State::Error, failure_error);
        return;
    }

    if (cancelRequested())
    {
        cleanup_task_context(fs, context, true);
        releaseWorkingBuffers();
        finish(State::Cancelled, Error::Cancelled);
        return;
    }

    updateProgress(input_size, input_size, true);

    bool finalize_ok = true;
    if (!context->output.seekSet(0))
    {
        finalize_ok = false;
        failure_error = Error::OutputSeekFailed;
    }
    else if (!write_wav_header(context->output, data_bytes))
    {
        finalize_ok = false;
        failure_error = Error::OutputWriteFailed;
    }
    else if (!context->output.sync())
    {
        finalize_ok = false;
        failure_error = Error::OutputWriteFailed;
    }

    context->input.close();
    context->output.close();
    context->output_opened = false;

    if (finalize_ok && cancelRequested())
    {
        cleanup_task_context(fs, context, true);
        releaseWorkingBuffers();
        finish(State::Cancelled, Error::Cancelled);
        return;
    }

    if (finalize_ok)
    {
        if (fs.exists(context->output_path) && !fs.remove(context->output_path))
        {
            finalize_ok = false;
            failure_error = Error::OutputRenameFailed;
        }
        else if (!fs.rename(context->temp_path, context->output_path))
        {
            finalize_ok = false;
            failure_error = Error::OutputRenameFailed;
        }
        else
        {
            context->temp_created = false;
        }
    }

    if (!finalize_ok)
    {
        cleanup_task_context(fs, context, true);
        releaseWorkingBuffers();
        finish(State::Error, failure_error);
        return;
    }

    cleanup_task_context(fs, context, false);
    releaseWorkingBuffers();
    finish(State::Done, Error::None);
}

} // namespace AudioFileRecorder
