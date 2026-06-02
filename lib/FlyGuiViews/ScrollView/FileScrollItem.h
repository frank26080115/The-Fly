#pragma once

#include "ScrollItem.h"
#include <stdint.h>

class FileScrollItem : public ScrollItem
{
public:
    FileScrollItem();

    void configureFile(ScrollItemKind kind, int32_t callbackValue, const char* fileName);

    uint32_t fileHash() const
    {
        return fileHash_;
    }

private:
    uint32_t fileHash_ = 0;
};
