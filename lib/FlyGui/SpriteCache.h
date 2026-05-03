#pragma once

#include <stdint.h>

struct FlyGuiSpriteBuffer
{
    const char* imagePath = nullptr;
    void*       buffer    = nullptr;
};

class FlyGuiSpriteCache
{
public:
    FlyGuiSpriteCache(FlyGuiSpriteBuffer* entries, uint8_t capacity);

    void* find(const char* imagePath) const;
    bool  remember(const char* imagePath, void* buffer);
    void  clear();

private:
    FlyGuiSpriteBuffer* entries_  = nullptr;
    uint8_t             capacity_ = 0;
};
