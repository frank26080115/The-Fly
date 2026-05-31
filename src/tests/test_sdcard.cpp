/*
This reports microSD card statistics, and then performs a speed test on the microSD card using the established recording module.

The slowest speed I saw is 136668 bytes/sec

For two mono 16 KHz streams being recorded, we want at least 64000 bytes/sec
*/

#include <Arduino.h>
#include <SdFat.h>
#include <new>
#include <string.h>

#include "AudioFileRecorder.h"
#include "AudioManager.h"
#include "MicroSdCard.h"
#include "Aegis.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG = "test_sdcard";
constexpr uint32_t    kTestSampleRateHz        = 16000;
constexpr uint32_t    kThroughputChunkStepMs   = 30000;
constexpr uint32_t    kThroughputReportMs      = 5000;

uint32_t g_prng = 0x51A7C0DE;

const char* card_type_name(uint8_t type)
{
    switch (type)
    {
    case SD_CARD_TYPE_SD1:
        return "SD1";
    case SD_CARD_TYPE_SD2:
        return "SD2";
    case SD_CARD_TYPE_SDHC:
        return "SDHC/SDXC";
    default:
        return "unknown";
    }
}

const char* fat_type_name(uint8_t type)
{
    if (type == FAT_TYPE_EXFAT)
    {
        return "exFAT";
    }
    if (type == 12 || type == 16 || type == 32)
    {
        return "FAT";
    }
    return "unknown";
}

uint16_t next_random_sample()
{
    g_prng = g_prng * 1664525UL + 1013904223UL;
    return static_cast<uint16_t>(g_prng >> 16);
}

void print_hex_bytes(const char* label, const void* data, size_t len)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);

    Serial.print(label);
    Serial.print(": ");
    for (size_t i = 0; i < len; ++i)
    {
        if (bytes[i] < 0x10)
        {
            Serial.print('0');
        }
        Serial.print(bytes[i], HEX);
    }
    Serial.println();
}

void print_card_stats()
{
    SdFs& sd = MicroSdCard::fs();

    cid_t    cid = {};
    csd_t    csd = {};
    scr_t    scr = {};
    uint32_t ocr = 0;

    Serial.println();
    Serial.println("microSD card statistics");
    Serial.printf("ready: yes\n");
    Serial.printf("card type: %s\n", card_type_name(sd.card()->type()));
    Serial.printf("sector count: %lu\n", static_cast<unsigned long>(sd.card()->sectorCount()));
    Serial.printf("raw capacity: %llu bytes\n", static_cast<unsigned long long>(static_cast<uint64_t>(sd.card()->sectorCount()) * 512ULL));
    Serial.printf("filesystem: %s%u\n", fat_type_name(sd.fatType()), sd.fatType() == FAT_TYPE_EXFAT ? 0 : sd.fatType());
    Serial.printf("sectors/cluster: %lu\n", static_cast<unsigned long>(sd.sectorsPerCluster()));
    Serial.printf("bytes/cluster: %lu\n", static_cast<unsigned long>(sd.bytesPerCluster()));
    Serial.printf("cluster count: %lu\n", static_cast<unsigned long>(sd.clusterCount()));
    Serial.printf("free clusters: %ld\n", static_cast<long>(sd.freeClusterCount()));
    Serial.printf("total bytes: %llu\n", static_cast<unsigned long long>(MicroSdCard::totalBytes()));
    Serial.printf("used bytes: %llu\n", static_cast<unsigned long long>(MicroSdCard::usedBytes()));
    Serial.printf("free bytes: %llu\n", static_cast<unsigned long long>(MicroSdCard::freeBytes()));

    if (sd.card()->readCID(&cid))
    {
        char product[6] = {};
        memcpy(product, cid.pnm, 5);
        Serial.printf("manufacturer ID: 0x%02X\n", cid.mid);
        Serial.printf("OEM ID: %c%c\n", cid.oid[0], cid.oid[1]);
        Serial.printf("product: %s\n", product);
        Serial.printf("revision: %u.%u\n", cid.prvN(), cid.prvM());
        Serial.printf("serial: 0x%08lX\n", static_cast<unsigned long>(cid.psn()));
        Serial.printf("manufactured: %u/%u\n", cid.mdtMonth(), cid.mdtYear());
        print_hex_bytes("CID", &cid, sizeof(cid));
    }
    else
    {
        Serial.println("CID: read failed");
    }

    if (sd.card()->readCSD(&csd))
    {
        Serial.printf("CSD capacity sectors: %lu\n", static_cast<unsigned long>(csd.capacity()));
        Serial.printf("erase size blocks: %lu\n", static_cast<unsigned long>(csd.eraseSize()));
        Serial.printf("erase single block: %s\n", csd.eraseSingleBlock() ? "yes" : "no");
        print_hex_bytes("CSD", &csd, sizeof(csd));
    }
    else
    {
        Serial.println("CSD: read failed");
    }

    if (sd.card()->readOCR(&ocr))
    {
        Serial.printf("OCR: 0x%08lX\n", static_cast<unsigned long>(ocr));
    }
    else
    {
        Serial.println("OCR: read failed");
    }

    if (sd.card()->readSCR(&scr))
    {
        Serial.printf("SD spec version: %u.%02u\n", scr.sdSpecVer() / 100, scr.sdSpecVer() % 100);
        Serial.printf("data after erase: %s\n", scr.dataAfterErase() ? "ones" : "zeros");
        print_hex_bytes("SCR", &scr, sizeof(scr));
    }
    else
    {
        Serial.println("SCR: read failed");
    }

    if (sd.sdErrorCode())
    {
        Serial.printf("SD error code: 0x%02X data=0x%02X\n", sd.sdErrorCode(), sd.sdErrorData());
    }
    Serial.println();
}

void fill_random_samples(int16_t* samples, size_t sample_count)
{
    for (size_t i = 0; i < sample_count; ++i)
    {
        samples[i] = static_cast<int16_t>(next_random_sample());
    }
}

uint64_t queue_chunk(AudioFifo& fifo, int16_t* samples, size_t chunk_samples)
{
    fill_random_samples(samples, chunk_samples);

    size_t queued = 0;
    while (queued < chunk_samples)
    {
        const size_t written = fifo.queue(samples + queued, chunk_samples - queued, kTestSampleRateHz);
        queued += written;
        AudioFileRecorder::pump();
        if (written == 0)
        {
            taskYIELD();
        }
    }

    return queued;
}

void run_recording_throughput_test()
{
    AudioFifo& bt_fifo  = AudioManager::bluetoothToFileFifo();
    AudioFifo& mic_fifo = AudioManager::micToFileFifo();

    const size_t bt_capacity_samples  = bt_fifo.availableToWrite();
    const size_t mic_capacity_samples = mic_fifo.availableToWrite();
    const size_t max_capacity_samples = max<size_t>(min(bt_capacity_samples, mic_capacity_samples), 1);
    size_t       chunk_samples        = min<size_t>(64, max_capacity_samples);

    int16_t* bt_samples  = new (std::nothrow) int16_t[max_capacity_samples];
    int16_t* mic_samples = new (std::nothrow) int16_t[max_capacity_samples];
    if (!bt_samples || !mic_samples)
    {
        delete[] bt_samples;
        delete[] mic_samples;
        Serial.printf("%s: sample chunk allocation failed: chunk=%u samples\n", TAG, static_cast<unsigned>(max_capacity_samples));
        return;
    }

    Serial.printf("%s: throughput test: step=%lu ms report=%lu ms chunk=%u max_chunk=%u samples\n",
                  TAG,
                  static_cast<unsigned long>(kThroughputChunkStepMs),
                  static_cast<unsigned long>(kThroughputReportMs),
                  static_cast<unsigned>(chunk_samples),
                  static_cast<unsigned>(max_capacity_samples));

    const uint32_t started_ms         = millis();
    uint32_t       next_report_ms     = started_ms + kThroughputReportMs;
    uint32_t       last_report_ms     = started_ms;
    uint32_t       section_started_ms = started_ms;
    uint64_t       total_samples      = 0;
    uint64_t       report_samples     = 0;
    bool           finished           = false;

    AudioFileRecorder::resetWriteDurationStats();
    Serial.printf("%s: throughput section: chunk=%u samples\n", TAG, static_cast<unsigned>(chunk_samples));

    while (!finished)
    {
        const uint64_t bt_submitted = queue_chunk(bt_fifo, bt_samples, chunk_samples);
        total_samples += bt_submitted;
        report_samples += bt_submitted;

        const uint64_t mic_submitted = queue_chunk(mic_fifo, mic_samples, chunk_samples);
        total_samples += mic_submitted;
        report_samples += mic_submitted;
        AudioFileRecorder::pump();

        const uint32_t now_ms = millis();
        if (static_cast<int32_t>(now_ms - next_report_ms) >= 0)
        {
            const uint32_t elapsed_ms = now_ms - last_report_ms;
            const uint64_t bytes_per_second = elapsed_ms == 0 ? 0 : ((report_samples * sizeof(int16_t) * 1000ULL) / elapsed_ms);

            Serial.printf("%s: chunk=%u submitted samples=%llu interval_bytes_per_second=%llu file_bytes=%llu write_avg_ms=%.3f write_max_ms=%.3f\n",
                          TAG,
                          static_cast<unsigned>(chunk_samples),
                          static_cast<unsigned long long>(total_samples),
                          static_cast<unsigned long long>(bytes_per_second),
                          static_cast<unsigned long long>(AudioFileRecorder::bytesWritten()),
                          AudioFileRecorder::writeDurationAverageMs(),
                          AudioFileRecorder::writeDurationMaxMs());

            report_samples = 0;
            last_report_ms = now_ms;
            next_report_ms = now_ms + kThroughputReportMs;
            AudioFileRecorder::resetWriteDurationStats();
        }

        if (static_cast<uint32_t>(now_ms - section_started_ms) >= kThroughputChunkStepMs)
        {
            if (chunk_samples >= max_capacity_samples)
            {
                finished = true;
            }
            else
            {
                chunk_samples = min(chunk_samples * 2, max_capacity_samples);
                section_started_ms = now_ms;
                report_samples = 0;
                last_report_ms = now_ms;
                next_report_ms = now_ms + kThroughputReportMs;
                AudioFileRecorder::resetWriteDurationStats();
                Serial.printf("%s: throughput section: chunk=%u samples\n", TAG, static_cast<unsigned>(chunk_samples));
            }
        }
    }

    AudioFileRecorder::pump();
    delete[] bt_samples;
    delete[] mic_samples;
}

} // namespace

void test_sdcard()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("starting microSD recorder smoke test");
    Serial.printf("free heap before FIFO init: %lu\n", static_cast<unsigned long>(ESP.getFreeHeap()));

    #if BUILD_WITH_SECURITY_LEVEL >= 1
    Serial.printf("%s: BUILD_WITH_SECURITY_LEVEL is %d; recorder WAV chunks should be AES-GCM encrypted\n", TAG, BUILD_WITH_SECURITY_LEVEL);
    if (!Aegis::isInitialized())
    {
        Serial.printf("%s: Aegis is not initialized\n", TAG);
        idle_forever();
    }
    if (!Aegis::hasMasterKey())
    {
        const uint8_t test_filecrypt_key[Aegis::kFilecryptKeySize] = {
            0x74, 0x68, 0x65, 0x66, 0x6c, 0x79, 0x2d, 0x73,
            0x64, 0x63, 0x61, 0x72, 0x64, 0x2d, 0x74, 0x65,
            0x73, 0x74, 0x2d, 0x6b, 0x65, 0x79, 0x2d, 0x30,
            0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31};
        const uint8_t test_network_key[Aegis::kNetworkKeySize] = {
            0x74, 0x68, 0x65, 0x66, 0x6c, 0x79, 0x2d, 0x6e,
            0x65, 0x74, 0x77, 0x6f, 0x72, 0x6b, 0x2d, 0x74,
            0x65, 0x73, 0x74, 0x2d, 0x6b, 0x65, 0x79, 0x2d,
            0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31};
        if (!Aegis::setFilecryptKey(test_filecrypt_key) || !Aegis::setNetworkKey(test_network_key))
        {
            Serial.printf("%s: Aegis test key setup failed\n", TAG);
            idle_forever();
        }
        Serial.printf("%s: installed deterministic test keys\n", TAG);
    }
    else
    {
        Serial.printf("%s: using existing Aegis keys\n", TAG);
    }
    #else
    Serial.printf("%s: BUILD_WITH_SECURITY_LEVEL is 0; recorder WAV chunks will not be encrypted\n", TAG);
    #endif

    if (!AudioManager::init())
    {
        Serial.printf("%s: AudioManager init failed\n", TAG);
        idle_forever();
    }
    Serial.printf("free heap after FIFO init: %lu\n", static_cast<unsigned long>(ESP.getFreeHeap()));

    if (!MicroSdCard::begin())
    {
        Serial.printf("%s: microSD init failed\n", TAG);
        idle_forever();
    }

    print_card_stats();

    AudioFileRecorder::setPurePcmMode(false);
    if (!AudioFileRecorder::startRecording('S'))
    {
        Serial.printf("%s: recording start failed\n", TAG);
        idle_forever();
    }

    Serial.printf("recording path: %s\n", AudioFileRecorder::currentSdPath());
    run_recording_throughput_test();

    if (!AudioFileRecorder::stopRecording())
    {
        Serial.printf("%s: recording stop failed\n", TAG);
        idle_forever();
    }

    Serial.printf("recording complete: %s bytes=%llu\n", AudioFileRecorder::currentSdPath(), static_cast<unsigned long long>(AudioFileRecorder::bytesWritten()));
    Serial.println("microSD recorder smoke test finished");

    idle_forever();
}
