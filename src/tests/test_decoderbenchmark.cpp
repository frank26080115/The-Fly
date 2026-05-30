#include <Arduino.h>
#include <SdFat.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "AudioFileDecoder.h"
#include "MicroSdCard.h"
#include "thefly_common.h"
#include "utilfuncs.h"

#if BUILD_WITH_SECURITY_LEVEL >= 1
#include "Aegis.h"
#include "mbedtls/gcm.h"
#endif

namespace
{

constexpr const char* TAG         = "test_decoderbenchmark";
constexpr const char* kInputPath  = "/decoder_bench.rec";
constexpr const char* kOutputPath = "/decoder_bench.wav";
constexpr const char* kTempPath   = "/temp.wav";

constexpr uint32_t kSampleRateHz       = 16000;
constexpr uint32_t kLeftFrequencyHz    = 440;
constexpr uint32_t kRightFrequencyHz   = 660;
constexpr int16_t  kAmplitude          = 12000;
constexpr size_t   kPacketSamples      = 240;
constexpr size_t   kSineTableBits      = 10;
constexpr size_t   kSineTableSize      = 1U << kSineTableBits;
constexpr uint32_t kGenerateReportMs   = 5000;
constexpr uint32_t kDecodeStopAfterMs  = 5UL * 60UL * 1000UL;
constexpr double   kPi                 = 3.14159265358979323846;

struct BenchmarkCase
{
    const char* label;
    uint32_t    seconds;
};

constexpr BenchmarkCase kCases[] = {
    { "10 seconds", 10 },
    { "1 minute",   60 },
    { "5 minutes",  5UL * 60UL },
    { "10 minutes", 10UL * 60UL },
    { "30 minutes", 30UL * 60UL },
    { "60 minutes", 60UL * 60UL },
};

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
              "encrypted benchmark packet must not contain padding");
#endif

int16_t       g_sine_table[kSineTableSize] = {};
file_packet_t g_packet = {};

#if BUILD_WITH_SECURITY_LEVEL >= 1
encrypted_file_packet_t g_encrypted_packet = {};
#endif

const char* g_current_label = "";
bool        g_decode_finished = false;
uint32_t    g_decode_started_ms = 0;
AudioFileRecorder::AudioFileDecoder::Status g_decode_status = {};

size_t min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

float elapsed_seconds(uint32_t started_ms, uint32_t now_ms)
{
    return static_cast<float>(now_ms - started_ms) / 1000.0f;
}

void init_sine_table()
{
    for (size_t i = 0; i < kSineTableSize; ++i)
    {
        const double radians = (static_cast<double>(i) * 2.0 * kPi) / static_cast<double>(kSineTableSize);
        g_sine_table[i] = static_cast<int16_t>(sinf(static_cast<float>(radians)) * static_cast<float>(kAmplitude));
    }
}

uint32_t phase_increment(uint32_t frequency_hz)
{
    return static_cast<uint32_t>((static_cast<uint64_t>(frequency_hz) << 32) / kSampleRateHz);
}

int16_t next_sample(uint32_t& phase, uint32_t increment)
{
    const size_t index = (phase >> (32U - kSineTableBits)) & (kSineTableSize - 1U);
    phase += increment;
    return g_sine_table[index];
}

void fill_packet_payload(uint32_t& phase, uint32_t increment, size_t samples)
{
    for (size_t i = 0; i < samples; ++i)
    {
        const int16_t sample = next_sample(phase, increment);
        memcpy(&g_packet.payload[i], &sample, sizeof(sample));
    }
}

#if BUILD_WITH_SECURITY_LEVEL >= 1
void store_u32_be(uint8_t* dst, uint32_t value)
{
    dst[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    dst[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dst[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dst[3] = static_cast<uint8_t>(value & 0xFF);
}

void packet_nonce(const file_packet_t& packet, uint8_t nonce[kGcmNonceSize])
{
    store_u32_be(nonce, packet.nonce_1);
    store_u32_be(nonce + 4, packet.nonce_2);
    store_u32_be(nonce + 8, packet.sequence_num);
}

bool setup_security()
{
    if (!Aegis::isInitialized() && !Aegis::init())
    {
        Serial.printf("%s: Aegis init failed\n", TAG);
        return false;
    }

    if (!Aegis::hasFilecryptKey())
    {
#ifdef BUILD_IS_DEBUG
        static const uint8_t test_filecrypt_key[Aegis::kFilecryptKeySize] = {
            0x74, 0x68, 0x65, 0x66, 0x6c, 0x79, 0x2d, 0x64,
            0x65, 0x63, 0x6f, 0x64, 0x65, 0x72, 0x2d, 0x74,
            0x65, 0x73, 0x74, 0x2d, 0x6b, 0x65, 0x79, 0x2d,
            0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31,
        };
        Aegis::setTestTempFilecryptKey(test_filecrypt_key);
        Serial.printf("%s: installed temporary deterministic filecrypt key\n", TAG);
#else
        Serial.printf("%s: filecrypt key is unavailable\n", TAG);
        return false;
#endif
    }

    return Aegis::getFilecryptKey() != nullptr;
}

bool setup_packet_encryption(mbedtls_gcm_context& gcm)
{
    const uint8_t* filecrypt_key = Aegis::getFilecryptKey();
    return filecrypt_key &&
           mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, filecrypt_key, Aegis::kFilecryptKeySize * 8) == 0;
}

bool encrypt_packet(mbedtls_gcm_context& gcm)
{
    packet_nonce(g_packet, g_encrypted_packet.nonce);
    return mbedtls_gcm_crypt_and_tag(&gcm,
                                     MBEDTLS_GCM_ENCRYPT,
                                     sizeof(g_packet),
                                     g_encrypted_packet.nonce,
                                     sizeof(g_encrypted_packet.nonce),
                                     nullptr,
                                     0,
                                     reinterpret_cast<const uint8_t*>(&g_packet),
                                     g_encrypted_packet.ciphertext,
                                     sizeof(g_encrypted_packet.tag),
                                     g_encrypted_packet.tag) == 0;
}
#endif

bool write_packet(FsFile& file,
                  filepkt_src_e source,
                  uint32_t timestamp_ms,
                  uint32_t sequence_num,
                  uint32_t sample_cursor,
                  uint32_t& phase,
                  uint32_t phase_inc,
                  size_t sample_count
#if BUILD_WITH_SECURITY_LEVEL >= 1
                  , mbedtls_gcm_context& gcm
#endif
)
{
    memset(&g_packet, 0, sizeof(g_packet));
    g_packet.magic          = FILE_PACKET_HEADER_MAGIC;
    g_packet.src            = source;
    g_packet.flags          = 0;
    g_packet.ms_timestamp   = timestamp_ms;
    g_packet.sequence_num   = sequence_num;
    g_packet.fifo_cnt       = static_cast<uint32_t>(sample_count);
    g_packet.payload_length = static_cast<uint16_t>(sample_count);

#if BUILD_WITH_SECURITY_LEVEL >= 1
    g_packet.nonce_1 = 0xD3C0D300UL ^ sequence_num;
    g_packet.nonce_2 = 0xBEEFB000UL ^ sample_cursor ^ (static_cast<uint32_t>(source) << 24);
#endif

    fill_packet_payload(phase, phase_inc, sample_count);

#if BUILD_WITH_SECURITY_LEVEL >= 1
    if (!encrypt_packet(gcm))
    {
        Serial.printf("%s: packet encryption failed at sequence=%lu\n", TAG, static_cast<unsigned long>(sequence_num));
        return false;
    }

    return file.write(reinterpret_cast<const uint8_t*>(&g_encrypted_packet), sizeof(g_encrypted_packet)) == sizeof(g_encrypted_packet);
#else
    return file.write(reinterpret_cast<const uint8_t*>(&g_packet), sizeof(g_packet)) == sizeof(g_packet);
#endif
}

bool remove_if_exists(SdFs& fs, const char* path)
{
    return !fs.exists(path) || fs.remove(path);
}

bool remove_benchmark_files()
{
    SdFs& fs = MicroSdCard::fs();
    return remove_if_exists(fs, kInputPath) &&
           remove_if_exists(fs, kOutputPath) &&
           remove_if_exists(fs, kTempPath);
}

const char* decoder_state_name(AudioFileRecorder::AudioFileDecoder::State state)
{
    switch (state)
    {
    case AudioFileRecorder::AudioFileDecoder::State::Idle:
        return "Idle";
    case AudioFileRecorder::AudioFileDecoder::State::Busy:
        return "Busy";
    case AudioFileRecorder::AudioFileDecoder::State::Done:
        return "Done";
    case AudioFileRecorder::AudioFileDecoder::State::Error:
        return "Error";
    case AudioFileRecorder::AudioFileDecoder::State::Cancelled:
        return "Cancelled";
    default:
        return "Unknown";
    }
}

const char* decoder_error_name(AudioFileRecorder::AudioFileDecoder::Error error)
{
    switch (error)
    {
    case AudioFileRecorder::AudioFileDecoder::Error::None:
        return "None";
    case AudioFileRecorder::AudioFileDecoder::Error::AlreadyBusy:
        return "AlreadyBusy";
    case AudioFileRecorder::AudioFileDecoder::Error::InvalidArgument:
        return "InvalidArgument";
    case AudioFileRecorder::AudioFileDecoder::Error::SdNotReady:
        return "SdNotReady";
    case AudioFileRecorder::AudioFileDecoder::Error::TaskCreateFailed:
        return "TaskCreateFailed";
    case AudioFileRecorder::AudioFileDecoder::Error::AllocationFailed:
        return "AllocationFailed";
    case AudioFileRecorder::AudioFileDecoder::Error::InputOpenFailed:
        return "InputOpenFailed";
    case AudioFileRecorder::AudioFileDecoder::Error::InputReadFailed:
        return "InputReadFailed";
    case AudioFileRecorder::AudioFileDecoder::Error::OutputOpenFailed:
        return "OutputOpenFailed";
    case AudioFileRecorder::AudioFileDecoder::Error::OutputWriteFailed:
        return "OutputWriteFailed";
    case AudioFileRecorder::AudioFileDecoder::Error::OutputSeekFailed:
        return "OutputSeekFailed";
    case AudioFileRecorder::AudioFileDecoder::Error::OutputRenameFailed:
        return "OutputRenameFailed";
    case AudioFileRecorder::AudioFileDecoder::Error::InvalidPacket:
        return "InvalidPacket";
    case AudioFileRecorder::AudioFileDecoder::Error::EncryptionSetupFailed:
        return "EncryptionSetupFailed";
    case AudioFileRecorder::AudioFileDecoder::Error::DecryptionFailed:
        return "DecryptionFailed";
    case AudioFileRecorder::AudioFileDecoder::Error::Cancelled:
        return "Cancelled";
    default:
        return "Unknown";
    }
}

bool generate_recording_file(const BenchmarkCase& bench)
{
    SdFs& fs = MicroSdCard::fs();
    if (!remove_benchmark_files())
    {
        Serial.printf("%s: failed to remove old benchmark files\n", TAG);
        return false;
    }

    FsFile file;
    if (!file.open(kInputPath, O_RDWR | O_CREAT | O_TRUNC))
    {
        Serial.printf("%s: open failed: %s\n", TAG, kInputPath);
        return false;
    }

#if BUILD_WITH_SECURITY_LEVEL >= 1
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    if (!setup_packet_encryption(gcm))
    {
        Serial.printf("%s: AES-GCM setup failed\n", TAG);
        mbedtls_gcm_free(&gcm);
        file.close();
        return false;
    }
#endif

    const uint64_t total_samples = static_cast<uint64_t>(bench.seconds) * kSampleRateHz;
    const uint32_t left_inc      = phase_increment(kLeftFrequencyHz);
    const uint32_t right_inc     = phase_increment(kRightFrequencyHz);
    uint32_t       left_phase    = 0;
    uint32_t       right_phase   = 0;
    uint32_t       sequence_num  = 0;
    uint64_t       sample_cursor = 0;
    uint32_t       next_report_ms = millis() + kGenerateReportMs;

    Serial.printf("%s: generating %s: left=%lu Hz right=%lu Hz samples_per_channel=%llu\n",
                  TAG,
                  bench.label,
                  static_cast<unsigned long>(kLeftFrequencyHz),
                  static_cast<unsigned long>(kRightFrequencyHz),
                  static_cast<unsigned long long>(total_samples));

    const uint32_t started_ms = millis();
    while (sample_cursor < total_samples)
    {
        const size_t samples = min_size(kPacketSamples, static_cast<size_t>(total_samples - sample_cursor));
        const uint32_t timestamp_ms = static_cast<uint32_t>((sample_cursor * 1000ULL) / kSampleRateHz);
        const uint32_t cursor32 = static_cast<uint32_t>(sample_cursor);

        if (!write_packet(file,
                          AUDSRC_MIC_16KHZ_MONO,
                          timestamp_ms,
                          sequence_num++,
                          cursor32,
                          left_phase,
                          left_inc,
                          samples
#if BUILD_WITH_SECURITY_LEVEL >= 1
                          , gcm
#endif
        ))
        {
            Serial.printf("%s: left packet write failed at sample=%llu\n", TAG, static_cast<unsigned long long>(sample_cursor));
#if BUILD_WITH_SECURITY_LEVEL >= 1
            mbedtls_gcm_free(&gcm);
#endif
            file.close();
            fs.remove(kInputPath);
            return false;
        }

        if (!write_packet(file,
                          AUDSRC_BT_16KHZ_MONO,
                          timestamp_ms,
                          sequence_num++,
                          cursor32,
                          right_phase,
                          right_inc,
                          samples
#if BUILD_WITH_SECURITY_LEVEL >= 1
                          , gcm
#endif
        ))
        {
            Serial.printf("%s: right packet write failed at sample=%llu\n", TAG, static_cast<unsigned long long>(sample_cursor));
#if BUILD_WITH_SECURITY_LEVEL >= 1
            mbedtls_gcm_free(&gcm);
#endif
            file.close();
            fs.remove(kInputPath);
            return false;
        }

        sample_cursor += samples;

        const uint32_t now_ms = millis();
        if (static_cast<int32_t>(now_ms - next_report_ms) >= 0)
        {
            const float pct = total_samples == 0 ? 100.0f : static_cast<float>((static_cast<double>(sample_cursor) * 100.0) / static_cast<double>(total_samples));
            Serial.printf("%s: generating %s progress %.1f%% samples=%llu/%llu elapsed=%.1fs\n",
                          TAG,
                          bench.label,
                          pct,
                          static_cast<unsigned long long>(sample_cursor),
                          static_cast<unsigned long long>(total_samples),
                          elapsed_seconds(started_ms, now_ms));
            next_report_ms = now_ms + kGenerateReportMs;
        }

        taskYIELD();
    }

    const bool synced = file.sync();
    const uint64_t file_size = file.fileSize();
    file.close();

#if BUILD_WITH_SECURITY_LEVEL >= 1
    mbedtls_gcm_free(&gcm);
#endif

    if (!synced)
    {
        Serial.printf("%s: generated recording sync failed\n", TAG);
        fs.remove(kInputPath);
        return false;
    }

    Serial.printf("%s: generated %s: file=%s bytes=%llu packets=%lu elapsed=%.1fs\n",
                  TAG,
                  bench.label,
                  kInputPath,
                  static_cast<unsigned long long>(file_size),
                  static_cast<unsigned long>(sequence_num),
                  elapsed_seconds(started_ms, millis()));
    return true;
}

void on_decode_progress(const AudioFileRecorder::AudioFileDecoder::Status& status)
{
    Serial.printf("%s: decoding %s progress %.1f%% bytes=%llu/%llu elapsed=%.1fs\n",
                  TAG,
                  g_current_label,
                  status.progress,
                  static_cast<unsigned long long>(status.bytes_processed),
                  static_cast<unsigned long long>(status.bytes_total),
                  elapsed_seconds(g_decode_started_ms, millis()));
}

void on_decode_complete(const AudioFileRecorder::AudioFileDecoder::Status& status)
{
    g_decode_status = status;
    g_decode_finished = true;
    Serial.printf("%s: decode callback %s state=%s error=%s elapsed=%.1fs\n",
                  TAG,
                  g_current_label,
                  decoder_state_name(status.state),
                  decoder_error_name(status.error),
                  elapsed_seconds(g_decode_started_ms, millis()));
}

bool decode_recording_file(const BenchmarkCase& bench, uint32_t& decode_elapsed_ms)
{
    g_current_label = bench.label;
    g_decode_finished = false;
    g_decode_status = {};

    AudioFileRecorder::AudioFileDecoder decoder(kInputPath, on_decode_complete, on_decode_progress);
    g_decode_started_ms = millis();

    Serial.printf("%s: starting decode for %s\n", TAG, bench.label);
    if (!decoder.start())
    {
        decoder.poll();
        g_decode_status = decoder.status();
        decode_elapsed_ms = millis() - g_decode_started_ms;
        Serial.printf("%s: decoder start failed for %s state=%s error=%s elapsed=%.1fs\n",
                      TAG,
                      bench.label,
                      decoder.stateName(),
                      decoder.errorName(),
                      elapsed_seconds(g_decode_started_ms, millis()));
        return false;
    }

    while (!g_decode_finished)
    {
        decoder.poll();

        const AudioFileRecorder::AudioFileDecoder::Status status = decoder.status();
        if (status.finished && !g_decode_finished)
        {
            decoder.poll();
        }

        taskYIELD();
        delay(1);
    }

    decoder.poll();
    decode_elapsed_ms = millis() - g_decode_started_ms;

    Serial.printf("%s: decoded %s in %.3f seconds: output=%s progress=%.1f%% bytes=%llu/%llu\n",
                  TAG,
                  bench.label,
                  static_cast<double>(decode_elapsed_ms) / 1000.0,
                  kOutputPath,
                  g_decode_status.progress,
                  static_cast<unsigned long long>(g_decode_status.bytes_processed),
                  static_cast<unsigned long long>(g_decode_status.bytes_total));

    return g_decode_status.state == AudioFileRecorder::AudioFileDecoder::State::Done &&
           g_decode_status.error == AudioFileRecorder::AudioFileDecoder::Error::None;
}

void print_file_size(const char* path)
{
    FsFile file;
    if (!file.open(path, O_RDONLY))
    {
        Serial.printf("%s: %s missing\n", TAG, path);
        return;
    }

    Serial.printf("%s: %s size=%llu bytes\n", TAG, path, static_cast<unsigned long long>(file.fileSize()));
    file.close();
}

} // namespace

void test_decoderbenchmark()
{
    Serial.println();
    Serial.printf("%s: starting decoder benchmark\n", TAG);
    Serial.printf("%s: BUILD_WITH_SECURITY_LEVEL=%d\n", TAG, BUILD_WITH_SECURITY_LEVEL);

    init_sine_table();

    if (!MicroSdCard::begin())
    {
        Serial.printf("%s: microSD init failed\n", TAG);
        idle_forever();
    }

#if BUILD_WITH_SECURITY_LEVEL >= 1
    if (!setup_security())
    {
        idle_forever();
    }
#endif

    bool stop_after_current = false;
    for (const BenchmarkCase& bench : kCases)
    {
        if (stop_after_current)
        {
            break;
        }

        if (!generate_recording_file(bench))
        {
            Serial.printf("%s: generation failed for %s; stopping benchmark\n", TAG, bench.label);
            break;
        }

        uint32_t decode_elapsed_ms = 0;
        if (!decode_recording_file(bench, decode_elapsed_ms))
        {
            Serial.printf("%s: decode failed for %s; stopping benchmark\n", TAG, bench.label);
            print_file_size(kInputPath);
            print_file_size(kOutputPath);
            break;
        }

        print_file_size(kInputPath);
        print_file_size(kOutputPath);

        if (decode_elapsed_ms > kDecodeStopAfterMs)
        {
            Serial.printf("%s: %s decode exceeded %.1f seconds; not running later cases\n",
                          TAG,
                          bench.label,
                          static_cast<double>(kDecodeStopAfterMs) / 1000.0);
            stop_after_current = true;
        }
    }

    Serial.printf("%s: decoder benchmark finished\n", TAG);
    idle_forever();
}
