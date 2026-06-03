#pragma once

#include "ScrollItem.h"
#include <stdint.h>

class FileScrollItem : public ScrollItem
{
public:
    FileScrollItem();

    void configureFile(ScrollItemKind kind, int32_t callbackValue, const char* fileName, uint64_t fileSize);

    uint32_t fileHash() const
    {
        return fileHash_;
    }

    uint64_t fileSize() const
    {
        return fileSize_;
    }

    const char* detailText() const override
    {
        return detailText_;
    }

private:
    uint32_t fileHash_       = 0;
    uint64_t fileSize_       = 0;
    char     detailText_[32] = {};
};
