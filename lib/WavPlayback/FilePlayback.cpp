#include "FilePlayback.h"

#include <algorithm>
#include <new>
#include <string.h>

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
#include "Aegis.h"
#include "mbedtls/gcm.h"
#endif

#include "AudioManager.h"
#include "MicroSdCard.h"
#include "Mp3EncryptedPlayback.h"
#include "Mp3Playback.h"
#include "WavEncryptedPlayback.h"
#include "WavPlayback.h"
#include "dbg_log.h"

namespace
{

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

    const size_t text_length = strlen(text);
    const size_t suffix_length = strlen(suffix);
    if (text_length < suffix_length)
    {
        return false;
    }

    return equals_case_insensitive(text + text_length - suffix_length, suffix);
}

} // namespace

std::unique_ptr<FilePlayback> FilePlayback::createForPath(const char* path)
{
    if (ends_with_case_insensitive(path, ".wav"))
    {
        return std::unique_ptr<FilePlayback>(new (std::nothrow) WavPlayback());
    }
    if (ends_with_case_insensitive(path, ".rec"))
    {
        return std::unique_ptr<FilePlayback>(new (std::nothrow) WavEncryptedPlayback());
    }
    if (ends_with_case_insensitive(path, ".mp3"))
    {
        return std::unique_ptr<FilePlayback>(new (std::nothrow) Mp3Playback());
    }
    if (ends_with_case_insensitive(path, ".fly"))
    {
        return std::unique_ptr<FilePlayback>(new (std::nothrow) Mp3EncryptedPlayback());
    }

    return nullptr;
}

FilePlayback::~FilePlayback()
{
    if (file_)
    {
        file_.close();
    }
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    endDecryption();
#endif
}

bool FilePlayback::start(const char* playbackPath)
{
    stop();

    std::lock_guard<std::mutex> lock(mutex_);
    setError("");

    if (!playbackPath || playbackPath[0] == '\0')
    {
        setError("empty path");
        return false;
    }

    if (!MicroSdCard::isReady())
    {
        setError("microSD not ready");
        return false;
    }

    strlcpy(path_, playbackPath, sizeof(path_));
    if (!file_.open(path_, O_RDONLY))
    {
        setError("open failed");
        DBG_LOGW(tag(), "open failed: %s", path_);
        path_[0] = '\0';
        return false;
    }

    if (!beginSource())
    {
        DBG_LOGW(tag(), "playback setup failed: %s path=%s", error_, path_);
        closeFileAndSource();
        path_[0] = '\0';
        return false;
    }

    active_ = true;
    eof_ = sourceDurationMs() == 0;
    finished_ = eof_;
    playing_ = !eof_;

    if (!setupSpeaker() || !seekToTimeMs(0))
    {
        closeFileAndSource();
        active_ = false;
        playing_ = false;
        path_[0] = '\0';
        return false;
    }

    DBG_LOGI(tag(),
             "started: %s duration=%lu ms",
             path_,
             static_cast<unsigned long>(sourceDurationMs()));
    return true;
}

void FilePlayback::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    closeFileAndSource();

    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    fifo.clear();
    fifo.setWatermark(kSpeakerWatermarkSamples);
    AudioManager::stop();

    active_ = false;
    playing_ = false;
    finished_ = false;
    eof_ = false;
    path_[0] = '\0';
}

void FilePlayback::pump()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !playing_ || !file_)
    {
        return;
    }

    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    if (eof_ || sourceAtEnd())
    {
        eof_ = true;
        fifo.setWatermark(0);
        if (fifo.usedSamples() == 0)
        {
            finish();
        }
        return;
    }

    if (!pumpSource(kPumpTargetFrames))
    {
        markEof();
    }

    if (eof_ || sourceAtEnd())
    {
        eof_ = true;
        fifo.setWatermark(0);
    }
}

bool FilePlayback::active() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

bool FilePlayback::playing() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return active_ && playing_;
}

bool FilePlayback::paused() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return active_ && !playing_;
}

bool FilePlayback::finished() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return finished_;
}

void FilePlayback::setPlaying(bool shouldPlay)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_)
    {
        return;
    }

    if (!shouldPlay)
    {
        if (playing_)
        {
            clearFifoAndMaybeRewind(true);
        }
        playing_ = false;
        return;
    }

    if (finished_ || sourceAtEnd())
    {
        seekToTimeMs(0);
        AudioManager::bluetoothToSpeakerFifo().clear();
    }

    finished_ = false;
    eof_ = sourceAtEnd();
    playing_ = !eof_;
    AudioManager::bluetoothToSpeakerFifo().setWatermark(kSpeakerWatermarkSamples);
}

void FilePlayback::togglePlaying()
{
    setPlaying(!playing());
}

void FilePlayback::setPositionMs(uint32_t positionMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_)
    {
        return;
    }

    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    fifo.clear();
    fifo.setWatermark(kSpeakerWatermarkSamples);

    if (!seekToTimeMs(positionMs))
    {
        markEof();
        return;
    }

    finished_ = false;
    eof_ = sourceAtEnd();
    if (eof_)
    {
        playing_ = false;
        finished_ = true;
    }
}

void FilePlayback::setVolume(uint8_t nextVolume)
{
    std::lock_guard<std::mutex> lock(mutex_);
    volume_ = nextVolume > AudioManager::kMaxVolume ? AudioManager::kMaxVolume : nextVolume;
    AudioManager::setVolume(volume_);
}

uint32_t FilePlayback::durationMs() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sourceDurationMs();
}

uint32_t FilePlayback::positionMs() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return clampedSourcePositionMs();
}

uint8_t FilePlayback::volume() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return volume_;
}

const char* FilePlayback::path() const
{
    return path_;
}

const char* FilePlayback::lastError() const
{
    return error_;
}

void FilePlayback::endSource()
{
}

FsFile& FilePlayback::file()
{
    return file_;
}

const FsFile& FilePlayback::file() const
{
    return file_;
}

void FilePlayback::setError(const char* error)
{
    strlcpy(error_, error ? error : "", sizeof(error_));
}

void FilePlayback::markEof()
{
    eof_ = true;
    AudioManager::bluetoothToSpeakerFifo().setWatermark(0);
}

bool FilePlayback::eofMarked() const
{
    return eof_;
}

void FilePlayback::resetChannelActivity()
{
    left_zero_run_ = kInactiveZeroRunFrames;
    right_zero_run_ = kInactiveZeroRunFrames;
}

size_t FilePlayback::framesAvailableToQueue()
{
    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    const size_t available = fifo.availableToWrite();
    return available >= kSpeakerWatermarkSamples ? available : 0;
}

size_t FilePlayback::queueMonoSamples(const int16_t* samples, size_t frames, uint32_t sampleRateHz)
{
    if (!samples || frames == 0)
    {
        return 0;
    }

    return AudioManager::bluetoothToSpeakerFifo().queue(samples, frames, sampleRateHz);
}

size_t FilePlayback::queuePcmFrames(const int16_t* samples, size_t frames, uint8_t channels, uint32_t sampleRateHz)
{
    if (!samples || frames == 0 || channels == 0)
    {
        return 0;
    }

    size_t queued_total = 0;
    while (queued_total < frames)
    {
        const size_t batch_frames = std::min(kMonoQueueBufferFrames, frames - queued_total);
        if (channels == 1)
        {
            memcpy(mono_buffer_, samples + queued_total, batch_frames * sizeof(int16_t));
        }
        else
        {
            const int16_t* batch = samples + queued_total * channels;
            for (size_t i = 0; i < batch_frames; ++i)
            {
                mono_buffer_[i] = mixStereoFrameToMono(batch + i * channels);
            }
        }

        const size_t queued = queueMonoSamples(mono_buffer_, batch_frames, sampleRateHz);
        queued_total += queued;
        if (queued < batch_frames)
        {
            break;
        }
    }

    return queued_total;
}

int16_t FilePlayback::mixStereoFrameToMono(const int16_t* frame)
{
    const int16_t left = frame[0];
    const int16_t right = frame[1];

    left_zero_run_ = left == 0 ? left_zero_run_ + 1 : 0;
    right_zero_run_ = right == 0 ? right_zero_run_ + 1 : 0;

    const bool left_active = left_zero_run_ < kInactiveZeroRunFrames;
    const bool right_active = right_zero_run_ < kInactiveZeroRunFrames;

    if (left_active && right_active)
    {
        return static_cast<int16_t>((static_cast<int32_t>(left) + static_cast<int32_t>(right)) / 2);
    }
    if (left_active)
    {
        return left;
    }
    if (right_active)
    {
        return right;
    }

    return 0;
}

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
bool FilePlayback::beginDecryption()
{
    endDecryption();

    if (!Aegis::isInitialized())
    {
        setError("Aegis not ready");
        return false;
    }

    const uint8_t* filecrypt_key = Aegis::getFilecryptKey();
    if (!filecrypt_key)
    {
        setError("file key missing");
        return false;
    }

    mbedtls_gcm_init(&playback_gcm_);
    if (mbedtls_gcm_setkey(&playback_gcm_, MBEDTLS_CIPHER_ID_AES, filecrypt_key, Aegis::kFilecryptKeySize * 8) != 0)
    {
        mbedtls_gcm_free(&playback_gcm_);
        setError("decrypt setup failed");
        return false;
    }

    playback_gcm_ready_ = true;
    return true;
}

void FilePlayback::endDecryption()
{
    if (playback_gcm_ready_)
    {
        mbedtls_gcm_free(&playback_gcm_);
        playback_gcm_ready_ = false;
    }
}

bool FilePlayback::decryptChunk(const uint8_t* encrypted, size_t plaintextSize, uint8_t* plaintext)
{
    if (!playback_gcm_ready_)
    {
        setError("decrypt not ready");
        return false;
    }
    if (!encrypted || !plaintext)
    {
        setError("decrypt buffer missing");
        return false;
    }

    const uint8_t* nonce = encrypted;
    const uint8_t* ciphertext = encrypted + RECORDER_ENCRYPTED_CHUNK_NONCE_LENGTH;
    const uint8_t* tag = ciphertext + plaintextSize;
    if (mbedtls_gcm_auth_decrypt(&playback_gcm_,
                                 plaintextSize,
                                 nonce,
                                 RECORDER_ENCRYPTED_CHUNK_NONCE_LENGTH,
                                 nullptr,
                                 0,
                                 tag,
                                 RECORDER_ENCRYPTED_CHUNK_TAG_LENGTH,
                                 ciphertext,
                                 plaintext) != 0)
    {
        setError("decrypt failed");
        return false;
    }

    return true;
}
#endif

void FilePlayback::finish()
{
    playing_ = false;
    finished_ = true;
    eof_ = true;
    AudioManager::bluetoothToSpeakerFifo().setWatermark(kSpeakerWatermarkSamples);
}

void FilePlayback::clearFifoAndMaybeRewind(bool rewindQueuedAudio)
{
    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    uint32_t rewind_ms = 0;
    if (rewindQueuedAudio)
    {
        rewind_ms = static_cast<uint32_t>((static_cast<uint64_t>(fifo.usedSamples()) * 1000ULL) / kSampleRateHz);
    }

    fifo.clear();
    fifo.setWatermark(kSpeakerWatermarkSamples);

    if (rewind_ms > 0)
    {
        const uint32_t current = clampedSourcePositionMs();
        seekToTimeMs(rewind_ms > current ? 0 : current - rewind_ms);
    }
}

bool FilePlayback::setupSpeaker()
{
    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    fifo.clear();
    fifo.setWatermark(kSpeakerWatermarkSamples);
    AudioManager::setSpeakerMuted(false);
    AudioManager::setVolume(volume_);
    if (!AudioManager::enableSpeakerMode())
    {
        setError("speaker failed");
        return false;
    }

    return true;
}

void FilePlayback::closeFileAndSource()
{
    if (file_)
    {
        file_.close();
    }
    endSource();
}

uint32_t FilePlayback::clampedSourcePositionMs() const
{
    const uint32_t total = sourceDurationMs();
    const uint32_t current = sourcePositionMs();
    return current > total ? total : current;
}
