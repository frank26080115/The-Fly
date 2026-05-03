#include "SpriteCache.h"

#include <string.h>

FlyGuiSpriteCache::FlyGuiSpriteCache(FlyGuiSpriteBuffer* entries, uint8_t capacity) : entries_(entries), capacity_(capacity) {}

void* FlyGuiSpriteCache::find(const char* imagePath) const
{
    // Design: shared image buffers prevent loading the same PNG into RAM repeatedly.
    if (!entries_ || !imagePath)
    {
        return nullptr;
    }

    for (uint8_t idx = 0; idx < capacity_; ++idx)
    {
        if (entries_[idx].buffer && entries_[idx].imagePath && strcmp(entries_[idx].imagePath, imagePath) == 0)
        {
            return entries_[idx].buffer;
        }
    }

    return nullptr;
}

bool FlyGuiSpriteCache::remember(const char* imagePath, void* buffer)
{
    if (!entries_ || !imagePath || !buffer)
    {
        return false;
    }

    for (uint8_t idx = 0; idx < capacity_; ++idx)
    {
        if (!entries_[idx].buffer)
        {
            entries_[idx].imagePath = imagePath;
            entries_[idx].buffer    = buffer;
            return true;
        }
    }

    return false;
}

void FlyGuiSpriteCache::clear()
{
    if (!entries_)
    {
        return;
    }

    for (uint8_t idx = 0; idx < capacity_; ++idx)
    {
        entries_[idx].imagePath = nullptr;
        entries_[idx].buffer    = nullptr;
    }
}
