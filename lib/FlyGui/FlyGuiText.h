#pragma once

#include "FlyGui.h"

class FlyGuiText : public FlyGuiItem
{
public:
    FlyGuiText(int16_t x, int16_t y, int16_t width, int16_t height,
               float fontSize, uint8_t fontStyle, size_t maxLength,
               const char* initialText = nullptr);
    virtual ~FlyGuiText();

    bool setText(const char* text);
    const char* text() const { return text_; }

    void redraw(M5GFX& display, bool forced) override;

protected:
    void updateRememberedText();
    size_t maxLength() const { return maxLength_; }

private:
    void drawTextRun(M5GFX& display, size_t start, size_t end);
    int32_t textPrefixWidth(M5GFX& display, const char* text, size_t length) const;
    int32_t textHeight() const;

    float fontSize_ = 1.0f;
    uint8_t fontStyle_ = 1;
    size_t maxLength_ = 0;
    char* text_ = nullptr;
    char* drawnText_ = nullptr;
};

class FlyGuiDateTime : public FlyGuiText
{
public:
    FlyGuiDateTime(int16_t x, int16_t y, int16_t width, int16_t height,
                   float fontSize, uint8_t fontStyle);

    void redraw(M5GFX& display, bool forced) override;
};

class FlyGuiStopwatch : public FlyGuiText
{
public:
    FlyGuiStopwatch(int16_t x, int16_t y, int16_t width, int16_t height,
                    float fontSize, uint8_t fontStyle);

    void start(uint32_t startMs = millis());
    void reset(uint32_t startMs = millis());
    uint32_t elapsedMs() const;
    void redraw(M5GFX& display, bool forced) override;

private:
    uint32_t startMs_ = 0;
};
