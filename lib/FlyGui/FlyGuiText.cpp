#include "FlyGuiText.h"

#include <stdlib.h>
#include <string.h>

FlyGuiText::FlyGuiText(int16_t x, int16_t y, int16_t width, int16_t height,
                       float fontSize, uint8_t fontStyle, size_t maxLength,
                       const char* initialText) :
    FlyGuiItem(x, y, width, height, nullptr, initialText),
    fontSize_(fontSize),
    fontStyle_(fontStyle),
    maxLength_(maxLength)
{
    text_ = static_cast<char*>(calloc(maxLength_ + 1, sizeof(char)));
    drawnText_ = static_cast<char*>(calloc(maxLength_ + 1, sizeof(char)));
    if (initialText)
    {
        setText(initialText);
        drawnText_[0] = '\0';
    }
}

FlyGuiText::~FlyGuiText()
{
    free(text_);
    free(drawnText_);
}

bool FlyGuiText::setText(const char* text)
{
    if (!text_ || !drawnText_)
    {
        return false;
    }

    const char* safeText = text ? text : "";
    if (strncmp(text_, safeText, maxLength_) == 0 && strlen(safeText) <= maxLength_)
    {
        return false;
    }

    strncpy(text_, safeText, maxLength_);
    text_[maxLength_] = '\0';
    setDirty();
    return true;
}

void FlyGuiText::redraw(M5GFX& display, bool forced)
{
    if (!visible() || (!forced && !dirty()))
    {
        return;
    }

    display.setTextSize(fontSize_);
    display.setTextFont(fontStyle_);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextDatum(top_left);

    if (forced)
    {
        display.fillRect(x(), y(), width(), height(), TFT_BLACK);
        display.drawString(text_, x(), y());
        updateRememberedText();
        markClean();
        return;
    }

    // Design: FlyGuiText redraws only character ranges that changed.
    size_t start = 0;
    const size_t oldLength = strlen(drawnText_);
    const size_t newLength = strlen(text_);
    const size_t maxCompared = oldLength > newLength ? oldLength : newLength;

    while (start < maxCompared)
    {
        while (start < maxCompared && drawnText_[start] == text_[start])
        {
            ++start;
        }

        if (start >= maxCompared)
        {
            break;
        }

        size_t end = start + 1;
        while (end < maxCompared && drawnText_[end] != text_[end])
        {
            ++end;
        }

        drawTextRun(display, start, end);
        start = end;
    }

    updateRememberedText();
    markClean();
}

void FlyGuiText::updateRememberedText()
{
    if (!drawnText_ || !text_)
    {
        return;
    }

    strncpy(drawnText_, text_, maxLength_);
    drawnText_[maxLength_] = '\0';
}

void FlyGuiText::drawTextRun(M5GFX& display, size_t start, size_t end)
{
    const int32_t originX = x() + textPrefixWidth(display, text_, start);
    const int32_t eraseWidth = textPrefixWidth(display, drawnText_, end) -
                               textPrefixWidth(display, drawnText_, start);
    const int32_t drawWidth = textPrefixWidth(display, text_, end) -
                              textPrefixWidth(display, text_, start);
    const int32_t clearWidth = eraseWidth > drawWidth ? eraseWidth : drawWidth;

    display.fillRect(originX, y(), clearWidth + 2, textHeight(), TFT_BLACK);

    char saved = text_[end];
    text_[end] = '\0';
    display.drawString(text_ + start, originX, y());
    text_[end] = saved;
}

int32_t FlyGuiText::textPrefixWidth(M5GFX& display, const char* text, size_t length) const
{
    if (!text || length == 0)
    {
        return 0;
    }

    char saved = const_cast<char*>(text)[length];
    const_cast<char*>(text)[length] = '\0';
    const int32_t width = display.textWidth(text);
    const_cast<char*>(text)[length] = saved;
    return width;
}

int32_t FlyGuiText::textHeight() const
{
    return height() > 0 ? height() : static_cast<int32_t>(fontSize_ * 8.0f) + 4;
}

FlyGuiDateTime::FlyGuiDateTime(int16_t x, int16_t y, int16_t width, int16_t height,
                               float fontSize, uint8_t fontStyle) :
    FlyGuiText(x, y, width, height, fontSize, fontStyle, 19)
{
}

void FlyGuiDateTime::redraw(M5GFX& display, bool forced)
{
    // Design: FlyGuiDateTime always shows current date/time and keeps frequent draws quick.
    const m5::rtc_datetime_t now = M5.Rtc.getDateTime();
    char text[20];
    snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u",
             now.date.year, now.date.month, now.date.date,
             now.time.hours, now.time.minutes, now.time.seconds);
    setText(text);
    FlyGuiText::redraw(display, forced);
}

FlyGuiStopwatch::FlyGuiStopwatch(int16_t x, int16_t y, int16_t width, int16_t height,
                                 float fontSize, uint8_t fontStyle) :
    FlyGuiText(x, y, width, height, fontSize, fontStyle, 12),
    startMs_(millis())
{
}

void FlyGuiStopwatch::start(uint32_t startMs)
{
    startMs_ = startMs;
    setDirty();
}

void FlyGuiStopwatch::reset(uint32_t startMs)
{
    start(startMs);
}

uint32_t FlyGuiStopwatch::elapsedMs() const
{
    return millis() - startMs_;
}

void FlyGuiStopwatch::redraw(M5GFX& display, bool forced)
{
    // Design: FlyGuiStopwatch shows elapsed time since a starting time.
    const uint32_t elapsed = elapsedMs() / 1000;
    const uint32_t hours = elapsed / 3600;
    const uint32_t minutes = (elapsed / 60) % 60;
    const uint32_t seconds = elapsed % 60;

    char text[13];
    snprintf(text, sizeof(text), "%02lu:%02lu:%02lu",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
    setText(text);
    FlyGuiText::redraw(display, forced);
}
