#pragma once

#include "FlyGui.h"

namespace FlyGuiTextUtil
{

void drawWrappedText(const char* text, int16_t x, int16_t y, int16_t width, int16_t maxY, int16_t lineHeight);

} // namespace FlyGuiTextUtil

class FlyGuiText : public FlyGuiItem
{
public:
    FlyGuiText(int16_t     x,
               int16_t     y,
               int16_t     width,
               int16_t     height,
               float       fontSize,
               uint8_t     fontStyle,
               size_t      maxLength,
               const char* initialText = nullptr);
    virtual ~FlyGuiText();

    bool        setText(const char* text);
    const char* text() const
    {
        return text_;
    }
    void setTextColor(uint16_t color);
    void setClearOnUpdate(bool clearOnUpdate);

    bool redraw(bool forced) override;

protected:
    void   updateRememberedText();
    size_t maxLength() const
    {
        return maxLength_;
    }

private:
    void    drawTextRun(size_t start, size_t end);
    int32_t textPrefixWidth(const char* text, size_t length) const;
    int32_t textHeight() const;

    float   fontSize_      = 1.0f;
    uint8_t fontStyle_     = 1;
    uint16_t textColor_    = TFT_WHITE;
    size_t  maxLength_     = 0;
    char*   text_          = nullptr;
    char*   drawnText_     = nullptr;
    bool    clearOnUpdate_ = false;
    bool    colorDirty_    = false;
};

class FlyGuiDateTime : public FlyGuiText
{
public:
    FlyGuiDateTime(int16_t x, int16_t y, int16_t width, int16_t height, float fontSize, uint8_t fontStyle);

    bool redraw(bool forced) override;
};

class FlyGuiStopwatch : public FlyGuiText
{
public:
    FlyGuiStopwatch(int16_t x, int16_t y, int16_t width, int16_t height, float fontSize, uint8_t fontStyle);

    void     start(uint32_t startMs = millis());
    void     reset(uint32_t startMs = millis());
    uint32_t elapsedMs() const;
    bool     redraw(bool forced) override;

private:
    uint32_t startMs_ = 0;
};
