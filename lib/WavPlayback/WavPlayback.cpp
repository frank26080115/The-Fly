#include "WavPlayback.h"

#include <algorithm>
#include <mutex>
#include <new>
#include <string.h>

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
#include "Aegis.h"
#include "AudioFileRecorder.h"
#include "mbedtls/gcm.h"
#endif

#include "AudioManager.h"
#include "MicroSdCard.h"
#include "dbg_log.h"

namespace WavPlayback
{
namespace
{

constexpr const char* TAG = "WavPlayback";

constexpr uint32_t kSampleRateHz      = AudioManager::kSampleRateHz;
constexpr uint32_t kChannels          = 2;
constexpr uint32_t kBytesPerSample    = sizeof(int16_t);
constexpr uint32_t kBytesPerFrame     = kChannels * kBytesPerSample;
constexpr uint32_t kBytesPerSecond    = kSampleRateHz * kBytesPerFrame;
constexpr uint32_t kPumpTargetFrames  = kSampleRateHz / 2;
constexpr size_t   kReadChunkFrames   = 512;
constexpr size_t   kSpeakerWatermarkSamples = 240;
constexpr size_t   kInactiveZeroRunFrames = 8;

std::mutex g_mutex;
FsFile     g_file;
bool       g_active       = false;
bool       g_playing      = false;
bool       g_finished     = false;
bool       g_eof          = false;
uint64_t   g_file_size    = 0;
uint64_t   g_data_bytes   = 0;
uint64_t   g_position_bytes = 0;
uint8_t    g_volume       = AudioManager::kMaxVolume;
char       g_path[96]     = {};
char       g_error[96]    = {};
int16_t    g_read_buffer[kReadChunkFrames * kChannels];
int16_t    g_mono_buffer[kReadChunkFrames];
size_t     g_left_zero_run  = kInactiveZeroRunFrames;
size_t     g_right_zero_run = kInactiveZeroRunFrames;

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
constexpr uint64_t kNoEncryptedChunk = UINT64_MAX;

mbedtls_gcm_context g_playback_gcm;
bool                g_playback_gcm_ready = false;
bool                g_encrypted_file     = false;
uint64_t            g_loaded_encrypted_chunk = kNoEncryptedChunk;
#endif

uint64_t clamp_data_position(uint64_t positionBytes)
{
    if (positionBytes > g_data_bytes)
    {
        return g_data_bytes;
    }

    return positionBytes - (positionBytes % kBytesPerFrame);
}

uint64_t bytes_for_ms(uint32_t positionMs)
{
    const uint64_t frames = (static_cast<uint64_t>(positionMs) * kSampleRateHz) / 1000ULL;
    return clamp_data_position(frames * kBytesPerFrame);
}

uint32_t ms_for_bytes(uint64_t bytes)
{
    bytes = clamp_data_position(bytes);
    return static_cast<uint32_t>((bytes * 1000ULL) / kBytesPerSecond);
}

void set_error(const char* error)
{
    strlcpy(g_error, error ? error : "", sizeof(g_error));
}

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
bool equals_case_insensitive(const char* lhs, const char* rhs)
{
    if (!lhs || !rhs)
    {
        return false;
    }

    while (*lhs && *rhs)
    {
        char l = *lhs++;
        char r = *rhs++;
        if (l >= 'A' && l <= 'Z')
        {
            l = static_cast<char>(l - 'A' + 'a');
        }
        if (r >= 'A' && r <= 'Z')
        {
            r = static_cast<char>(r - 'A' + 'a');
        }
        if (l != r)
        {
            return false;
        }
    }

    return *lhs == '\0' && *rhs == '\0';
}

bool ends_with_case_insensitive(const char* text, const char* suffix)
{
    if (!text || !suffix)
    {
        return false;
    }

    const size_t textLength = strlen(text);
    const size_t suffixLength = strlen(suffix);
    if (textLength < suffixLength)
    {
        return false;
    }

    return equals_case_insensitive(text + textLength - suffixLength, suffix);
}

bool is_encrypted_recording_path(const char* path)
{
    return ends_with_case_insensitive(path, ".rec");
}

void reset_encrypted_chunk_locked()
{
    g_loaded_encrypted_chunk = kNoEncryptedChunk;
}

void free_playback_gcm_locked()
{
    if (g_playback_gcm_ready)
    {
        mbedtls_gcm_free(&g_playback_gcm);
        g_playback_gcm_ready = false;
    }
}

void stop_playback_decryption_locked()
{
    free_playback_gcm_locked();

    g_encrypted_file = false;
    reset_encrypted_chunk_locked();
}

bool start_playback_decryption_locked()
{
    free_playback_gcm_locked();
    reset_encrypted_chunk_locked();

    if (!Aegis::isInitialized() && !Aegis::init())
    {
        set_error("Aegis init failed");
        return false;
    }

    const uint8_t* filecryptKey = Aegis::getFilecryptKey();
    if (!filecryptKey)
    {
        set_error("file key missing");
        return false;
    }

    mbedtls_gcm_init(&g_playback_gcm);
    if (mbedtls_gcm_setkey(&g_playback_gcm, MBEDTLS_CIPHER_ID_AES, filecryptKey, Aegis::kFilecryptKeySize * 8) != 0)
    {
        mbedtls_gcm_free(&g_playback_gcm);
        set_error("decrypt setup failed");
        return false;
    }

    g_playback_gcm_ready = true;
    return true;
}

uint64_t encrypted_data_bytes_for_file_size(uint64_t fileSize)
{
    if (fileSize <= WAV_ENCRYPTED_RIFF_HEADER_LENGTH)
    {
        return 0;
    }

    const uint64_t encryptedAudioBytes = fileSize - WAV_ENCRYPTED_RIFF_HEADER_LENGTH;
    const uint64_t encryptedChunks = encryptedAudioBytes / WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH;
    return encryptedChunks * WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH;
}

bool decrypt_borrowed_buffer_locked(size_t plaintextSize)
{
    if (!g_playback_gcm_ready)
    {
        set_error("decrypt not ready");
        return false;
    }

    uint8_t* plaintext = AudioFileRecorder::wavPlaintextAudioBuffer();
    uint8_t* encrypted = AudioFileRecorder::wavEncryptedAudioBuffer();
    if (!plaintext || !encrypted)
    {
        set_error("decrypt buffer missing");
        return false;
    }
    if (AudioFileRecorder::wavPlaintextAudioBufferSize() < plaintextSize)
    {
        set_error("plain buffer small");
        return false;
    }

    const size_t encryptedSize = WAV_ENCRYPTED_CHUNK_NONCE_LENGTH + plaintextSize + WAV_ENCRYPTED_CHUNK_TAG_LENGTH;
    if (AudioFileRecorder::wavEncryptedAudioBufferSize() < encryptedSize)
    {
        set_error("enc buffer small");
        return false;
    }

    const uint8_t* nonce      = encrypted;
    const uint8_t* ciphertext = encrypted + WAV_ENCRYPTED_CHUNK_NONCE_LENGTH;
    const uint8_t* tag        = ciphertext + plaintextSize;
    if (mbedtls_gcm_auth_decrypt(&g_playback_gcm,
                                 plaintextSize,
                                 nonce,
                                 WAV_ENCRYPTED_CHUNK_NONCE_LENGTH,
                                 nullptr,
                                 0,
                                 tag,
                                 WAV_ENCRYPTED_CHUNK_TAG_LENGTH,
                                 ciphertext,
                                 plaintext) != 0)
    {
        set_error("decrypt failed");
        return false;
    }

    return true;
}

bool read_encrypted_block_locked(uint64_t filePosition, size_t plaintextSize)
{
    uint8_t* encrypted = AudioFileRecorder::wavEncryptedAudioBuffer();
    if (!encrypted)
    {
        set_error("enc buffer missing");
        return false;
    }

    const size_t encryptedSize = WAV_ENCRYPTED_CHUNK_NONCE_LENGTH + plaintextSize + WAV_ENCRYPTED_CHUNK_TAG_LENGTH;
    if (AudioFileRecorder::wavEncryptedAudioBufferSize() < encryptedSize)
    {
        set_error("enc buffer small");
        return false;
    }
    if (!g_file.seek(filePosition))
    {
        set_error("seek failed");
        return false;
    }

    const int bytesRead = g_file.read(encrypted, encryptedSize);
    if (bytesRead != static_cast<int>(encryptedSize))
    {
        set_error("read failed");
        return false;
    }

    return decrypt_borrowed_buffer_locked(plaintextSize);
}

bool wav_header_valid(const uint8_t* header)
{
    return header &&
           memcmp(header + 0, "RIFF", 4) == 0 &&
           memcmp(header + 8, "WAVE", 4) == 0 &&
           memcmp(header + 12, "fmt ", 4) == 0 &&
           memcmp(header + 36, "data", 4) == 0;
}

bool decrypt_encrypted_header_locked()
{
    if (!read_encrypted_block_locked(0, WAV_RIFF_HEADER_LENGTH))
    {
        return false;
    }

    if (!wav_header_valid(AudioFileRecorder::wavPlaintextAudioBuffer()))
    {
        set_error("bad WAV header");
        return false;
    }

    return true;
}

bool ensure_encrypted_audio_chunk_locked(uint64_t positionBytes)
{
    const uint64_t chunkIndex = positionBytes / WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH;
    if (g_loaded_encrypted_chunk == chunkIndex)
    {
        return true;
    }

    const uint64_t filePosition = static_cast<uint64_t>(WAV_ENCRYPTED_RIFF_HEADER_LENGTH) +
                                  chunkIndex * static_cast<uint64_t>(WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH);
    if (!read_encrypted_block_locked(filePosition, WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH))
    {
        reset_encrypted_chunk_locked();
        return false;
    }

    g_loaded_encrypted_chunk = chunkIndex;
    return true;
}
#else
void stop_playback_decryption_locked()
{
}
#endif

void reset_channel_activity_locked()
{
    g_left_zero_run  = kInactiveZeroRunFrames;
    g_right_zero_run = kInactiveZeroRunFrames;
}

bool seek_locked(uint64_t positionBytes)
{
    positionBytes = clamp_data_position(positionBytes);
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    if (g_encrypted_file)
    {
        positionBytes -= positionBytes % WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH;
        const uint64_t chunkIndex = positionBytes / WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH;
        const uint64_t filePosition = static_cast<uint64_t>(WAV_ENCRYPTED_RIFF_HEADER_LENGTH) +
                                      chunkIndex * static_cast<uint64_t>(WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH);
        if (!g_file.seek(filePosition))
        {
            set_error("seek failed");
            return false;
        }

        g_position_bytes = positionBytes;
        g_eof            = g_position_bytes >= g_data_bytes;
        g_finished       = false;
        reset_encrypted_chunk_locked();
        reset_channel_activity_locked();
        return true;
    }
#endif

    const uint64_t filePosition = static_cast<uint64_t>(kWavHeaderBytes) + positionBytes;
    if (!g_file.seek(filePosition))
    {
        set_error("seek failed");
        return false;
    }

    g_position_bytes = positionBytes;
    g_eof            = g_position_bytes >= g_data_bytes;
    g_finished       = false;
    reset_channel_activity_locked();
    return true;
}

void clear_fifo_and_rewind_locked(bool rewindQueuedAudio)
{
    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    uint64_t   rewindBytes = 0;
    if (rewindQueuedAudio)
    {
        rewindBytes = static_cast<uint64_t>(fifo.usedSamples()) * kBytesPerFrame;
    }

    fifo.clear();
    fifo.setWatermark(kSpeakerWatermarkSamples);

    if (rewindBytes > 0)
    {
        const uint64_t nextPosition = rewindBytes > g_position_bytes ? 0 : g_position_bytes - rewindBytes;
        seek_locked(nextPosition);
    }
}

void finish_locked()
{
    g_playing  = false;
    g_finished = true;
    g_eof      = true;
    g_position_bytes = g_data_bytes;
    AudioManager::bluetoothToSpeakerFifo().setWatermark(kSpeakerWatermarkSamples);
}

int16_t mix_stereo_frame_to_mono(const int16_t* frame)
{
    const int16_t left  = frame[0];
    const int16_t right = frame[1];

    g_left_zero_run  = left == 0 ? g_left_zero_run + 1 : 0;
    g_right_zero_run = right == 0 ? g_right_zero_run + 1 : 0;

    const bool leftActive  = g_left_zero_run < kInactiveZeroRunFrames;
    const bool rightActive = g_right_zero_run < kInactiveZeroRunFrames;

    if (leftActive && rightActive)
    {
        return static_cast<int16_t>((static_cast<int32_t>(left) + static_cast<int32_t>(right)) / 2);
    }
    if (leftActive)
    {
        return left;
    }
    if (rightActive)
    {
        return right;
    }

    return 0;
}

size_t frames_available_to_queue()
{
    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    const size_t available = fifo.availableToWrite();
    return available >= kSpeakerWatermarkSamples ? available : 0;
}

} // namespace

bool start(const char* playbackPath)
{
    stop();

    std::lock_guard<std::mutex> lock(g_mutex);
    set_error("");

    if (!playbackPath || playbackPath[0] == '\0')
    {
        set_error("empty path");
        return false;
    }

    if (!MicroSdCard::isReady())
    {
        set_error("microSD not ready");
        return false;
    }

    strlcpy(g_path, playbackPath, sizeof(g_path));
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    g_encrypted_file = is_encrypted_recording_path(g_path);
#endif
    if (!g_file.open(g_path, O_RDONLY))
    {
        set_error("open failed");
        DBG_LOGW(TAG, "open failed: %s", g_path);
        stop_playback_decryption_locked();
        g_path[0] = '\0';
        return false;
    }

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    if (g_encrypted_file)
    {
        if (!start_playback_decryption_locked() || !decrypt_encrypted_header_locked())
        {
            DBG_LOGW(TAG, "encrypted playback setup failed: %s path=%s", g_error, g_path);
            g_file.close();
            stop_playback_decryption_locked();
            g_path[0] = '\0';
            return false;
        }
    }
#endif

    g_file_size      = g_file.fileSize();
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    g_data_bytes     = g_encrypted_file ? encrypted_data_bytes_for_file_size(g_file_size)
                                        : (g_file_size > kWavHeaderBytes ? g_file_size - kWavHeaderBytes : 0);
#else
    g_data_bytes     = g_file_size > kWavHeaderBytes ? g_file_size - kWavHeaderBytes : 0;
#endif
    g_data_bytes     = clamp_data_position(g_data_bytes);
    g_position_bytes = 0;
    g_eof            = g_data_bytes == 0;
    g_finished       = g_eof;
    g_playing        = !g_eof;
    g_active         = true;

    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    fifo.clear();
    fifo.setWatermark(kSpeakerWatermarkSamples);
    AudioManager::setSpeakerMuted(false);
    AudioManager::setVolume(g_volume);
    if (!AudioManager::enableSpeakerMode())
    {
        set_error("speaker failed");
        g_file.close();
        stop_playback_decryption_locked();
        g_active = false;
        g_playing = false;
        g_path[0] = '\0';
        return false;
    }

    if (!seek_locked(0))
    {
        g_file.close();
        stop_playback_decryption_locked();
        g_active = false;
        g_playing = false;
        g_path[0] = '\0';
        return false;
    }

    DBG_LOGI(TAG,
             "started: %s bytes=%llu data=%llu duration=%lu ms",
             g_path,
             static_cast<unsigned long long>(g_file_size),
             static_cast<unsigned long long>(g_data_bytes),
             static_cast<unsigned long>(ms_for_bytes(g_data_bytes)));
    return true;
}

void stop()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file)
    {
        g_file.close();
    }
    stop_playback_decryption_locked();

    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    fifo.clear();
    fifo.setWatermark(kSpeakerWatermarkSamples);
    AudioManager::stop();

    g_active         = false;
    g_playing        = false;
    g_finished       = false;
    g_eof            = false;
    g_file_size      = 0;
    g_data_bytes     = 0;
    g_position_bytes = 0;
    g_path[0]        = '\0';
}

void pump()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_active || !g_playing || !g_file)
    {
        return;
    }

    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    if (g_eof)
    {
        fifo.setWatermark(0);
        if (fifo.usedSamples() == 0)
        {
            finish_locked();
        }
        return;
    }

    size_t framesRemainingThisPump = kPumpTargetFrames;
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    if (g_encrypted_file)
    {
        while (framesRemainingThisPump > 0 && !g_eof)
        {
            const size_t availableFrames = frames_available_to_queue();
            if (availableFrames == 0)
            {
                break;
            }

            const uint64_t bytesRemaining = g_data_bytes > g_position_bytes ? g_data_bytes - g_position_bytes : 0;
            if (bytesRemaining == 0)
            {
                g_eof = true;
                fifo.setWatermark(0);
                break;
            }

            if (!ensure_encrypted_audio_chunk_locked(g_position_bytes))
            {
                g_eof = true;
                fifo.setWatermark(0);
                break;
            }

            const size_t chunkOffset = static_cast<size_t>(g_position_bytes % WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH);
            const size_t chunkBytesRemaining = static_cast<size_t>(
                std::min<uint64_t>(WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH - chunkOffset, bytesRemaining));
            const size_t chunkFramesRemaining = chunkBytesRemaining / kBytesPerFrame;
            const size_t framesToRead = std::min({ kReadChunkFrames, framesRemainingThisPump, availableFrames, chunkFramesRemaining });
            if (framesToRead == 0)
            {
                g_eof = true;
                fifo.setWatermark(0);
                break;
            }

            const uint8_t* plaintext = AudioFileRecorder::wavPlaintextAudioBuffer();
            if (!plaintext)
            {
                set_error("plain buffer missing");
                g_eof = true;
                fifo.setWatermark(0);
                break;
            }
            const int16_t* stereoFrames = reinterpret_cast<const int16_t*>(plaintext + chunkOffset);
            for (size_t i = 0; i < framesToRead; ++i)
            {
                g_mono_buffer[i] = mix_stereo_frame_to_mono(&stereoFrames[i * kChannels]);
            }

            const size_t queued = fifo.queue(g_mono_buffer, framesToRead, kSampleRateHz);
            g_position_bytes += static_cast<uint64_t>(queued) * kBytesPerFrame;
            framesRemainingThisPump -= queued;

            if (queued < framesToRead)
            {
                break;
            }
            if (g_position_bytes >= g_data_bytes)
            {
                g_eof = true;
                fifo.setWatermark(0);
                break;
            }
        }
        return;
    }
#endif

    while (framesRemainingThisPump > 0 && !g_eof)
    {
        const size_t availableFrames = frames_available_to_queue();
        if (availableFrames == 0)
        {
            break;
        }

        const uint64_t bytesRemaining = g_data_bytes > g_position_bytes ? g_data_bytes - g_position_bytes : 0;
        if (bytesRemaining == 0)
        {
            g_eof = true;
            fifo.setWatermark(0);
            break;
        }

        const size_t fileFramesRemaining = static_cast<size_t>(std::min<uint64_t>(bytesRemaining / kBytesPerFrame, UINT32_MAX));
        const size_t framesToRead = std::min({ kReadChunkFrames, framesRemainingThisPump, availableFrames, fileFramesRemaining });
        if (framesToRead == 0)
        {
            g_eof = true;
            fifo.setWatermark(0);
            break;
        }

        const size_t bytesToRead = framesToRead * kBytesPerFrame;
        const int    bytesRead   = g_file.read(g_read_buffer, bytesToRead);
        if (bytesRead <= 0)
        {
            g_eof = true;
            fifo.setWatermark(0);
            break;
        }

        const size_t framesRead = static_cast<size_t>(bytesRead) / kBytesPerFrame;
        if (framesRead == 0)
        {
            g_eof = true;
            fifo.setWatermark(0);
            break;
        }

        for (size_t i = 0; i < framesRead; ++i)
        {
            g_mono_buffer[i] = mix_stereo_frame_to_mono(&g_read_buffer[i * kChannels]);
        }

        const size_t queued = fifo.queue(g_mono_buffer, framesRead, kSampleRateHz);
        g_position_bytes += static_cast<uint64_t>(queued) * kBytesPerFrame;
        if (queued < framesRead)
        {
            const uint64_t rewind = static_cast<uint64_t>(framesRead - queued) * kBytesPerFrame;
            seek_locked(g_position_bytes);
            if (rewind > 0)
            {
                break;
            }
        }

        framesRemainingThisPump -= queued;
        if (static_cast<size_t>(bytesRead) < bytesToRead || g_position_bytes >= g_data_bytes)
        {
            g_eof = true;
            fifo.setWatermark(0);
            break;
        }
    }
}

bool active()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_active;
}

bool playing()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_active && g_playing;
}

bool paused()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_active && !g_playing;
}

bool finished()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_finished;
}

void setPlaying(bool shouldPlay)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_active)
    {
        return;
    }

    if (!shouldPlay)
    {
        if (g_playing)
        {
            clear_fifo_and_rewind_locked(true);
        }
        g_playing = false;
        return;
    }

    if (g_finished || g_position_bytes >= g_data_bytes)
    {
        seek_locked(0);
        AudioManager::bluetoothToSpeakerFifo().clear();
    }

    g_finished = false;
    g_eof      = g_position_bytes >= g_data_bytes;
    g_playing  = !g_eof;
    AudioManager::bluetoothToSpeakerFifo().setWatermark(kSpeakerWatermarkSamples);
}

void togglePlaying()
{
    setPlaying(!playing());
}

void setPositionMs(uint32_t newPositionMs)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_active)
    {
        return;
    }

    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    fifo.clear();
    fifo.setWatermark(kSpeakerWatermarkSamples);
    seek_locked(bytes_for_ms(newPositionMs));
    g_finished = false;
    if (g_position_bytes >= g_data_bytes)
    {
        g_eof = true;
        g_playing = false;
        g_finished = true;
    }
}

void setVolume(uint8_t nextVolume)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_volume = nextVolume > AudioManager::kMaxVolume ? AudioManager::kMaxVolume : nextVolume;
    AudioManager::setVolume(g_volume);
}

uint32_t durationMs()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return ms_for_bytes(g_data_bytes);
}

uint32_t positionMs()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    const uint32_t current = ms_for_bytes(g_position_bytes);
    const uint32_t total   = ms_for_bytes(g_data_bytes);
    return current > total ? total : current;
}

uint8_t volume()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_volume;
}

const char* path()
{
    return g_path;
}

const char* lastError()
{
    return g_error;
}

} // namespace WavPlayback
