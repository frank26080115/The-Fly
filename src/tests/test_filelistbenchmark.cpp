#include <Arduino.h>
#include <SdFat.h>
#include <stdio.h>

#include "MicroSdCard.h"
#include "MostRecentFiles.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG              = "test_filelistbenchmark";
constexpr size_t      kTargetRootFiles = 50;
constexpr size_t      kMaxCreateTries  = 500;

size_t count_root_files()
{
    FsFile root;
    if (!root.open("/", O_RDONLY))
    {
        return 0;
    }

    size_t count = 0;
    FsFile child;
    while (child.openNext(&root, O_RDONLY))
    {
        if (child.isFile())
        {
            ++count;
        }
        child.close();
    }

    root.close();
    return count;
}

bool create_empty_file(const char* path)
{
    FsFile file;
    if (!file.open(path, O_WRONLY | O_CREAT | O_TRUNC))
    {
        return false;
    }

    const bool ok = file.sync();
    file.close();
    return ok;
}

bool ensure_root_file_count(size_t target_count)
{
    SdFs&  fs      = MicroSdCard::fs();
    size_t count   = count_root_files();
    size_t created = 0;

    Serial.printf("%s: root file count before setup=%u target=%u\n",
                  TAG,
                  static_cast<unsigned>(count),
                  static_cast<unsigned>(target_count));

    size_t suffix = 0;
    size_t tries  = 0;
    while (count < target_count && tries < kMaxCreateTries)
    {
        char path[MostRecentFiles::kMaxFileNameLength] = {};
        snprintf(path, sizeof(path), "/flbench-%03u.tmp", static_cast<unsigned>(suffix++));

        if (fs.exists(path))
        {
            ++tries;
            continue;
        }

        if (!create_empty_file(path))
        {
            Serial.printf("%s: failed to create %s\n", TAG, path);
            return false;
        }

        ++count;
        ++created;
        ++tries;
    }

    Serial.printf("%s: root file count after setup=%u created=%u\n",
                  TAG,
                  static_cast<unsigned>(count),
                  static_cast<unsigned>(created));

    return count >= target_count;
}

void run_filelist_case(size_t max_files)
{
    const uint32_t            started_us = micros();
    MostRecentFiles::FileList files      = MostRecentFiles::get(max_files);
    const uint32_t            elapsed_us = micros() - started_us;

    Serial.printf("%s: max_files=%u populated=%u elapsed=%lu.%03lu ms",
                  TAG,
                  static_cast<unsigned>(max_files),
                  static_cast<unsigned>(files.count),
                  static_cast<unsigned long>(elapsed_us / 1000UL),
                  static_cast<unsigned long>(elapsed_us % 1000UL));

    if (files.count > 0)
    {
        Serial.printf(" newest=%s", files[0]);
    }
    Serial.println();
}

} // namespace

void test_filelistbenchmark()
{
    Serial.printf("%s: starting file list benchmark\n", TAG);

    if (!MicroSdCard::begin())
    {
        Serial.printf("%s: microSD init failed\n", TAG);
        idle_forever();
    }

    if (!ensure_root_file_count(kTargetRootFiles))
    {
        Serial.printf("%s: could not create enough benchmark files\n", TAG);
        idle_forever();
    }

    for (size_t max_files = 10; max_files <= 50; max_files += 10)
    {
        run_filelist_case(max_files);
        taskYIELD();
    }

    Serial.printf("%s: file list benchmark finished\n", TAG);
    idle_forever();
}
