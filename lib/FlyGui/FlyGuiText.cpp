#include "FlyGuiText.h"

#include <stdlib.h>
#include <string.h>

#include "../BattTracker/BattTracker.h"
#include "ClockAgent.h"
#include "dbg_log.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

namespace
{

constexpr bool kFlyGuiDebugClock = static_cast<int>(DBG_LOG_LOCAL_LEVEL) > static_cast<int>(DBG_LOG_ERROR);

} // namespace

namespace FlyGuiTextUtil
{
namespace
{
constexpr size_t kLineMax = 128;
constexpr size_t kNoSpace = static_cast<size_t>(-1);

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

bool draw_line(char* line, size_t& len, int16_t x, int16_t& y, int16_t max_y, int16_t line_height);
void recalc_last_space(const char* line, size_t len, size_t& last_space);
void trim_leading_spaces(char* line, size_t& len);
} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void drawWrappedText(const char* text, int16_t x, int16_t y, int16_t width, int16_t maxY, int16_t lineHeight)
{
    if (!text || width <= 0 || lineHeight <= 0)
    {
        return;
    }

    char   line[kLineMax] = {};
    size_t len            = 0;
    size_t last_space     = kNoSpace;
    bool   can_draw       = true;

    for (const char* p = text; *p && can_draw; ++p)
    {
        const char c = *p;
        if (c == '\r')
        {
            continue;
        }

        if (c == '\n')
        {
            can_draw   = draw_line(line, len, x, y, maxY, lineHeight);
            last_space = kNoSpace;
            continue;
        }

        if (len + 1 >= sizeof(line))
        {
            can_draw   = draw_line(line, len, x, y, maxY, lineHeight);
            last_space = kNoSpace;
            if (!can_draw)
            {
                break;
            }
        }

        line[len++] = c;
        line[len]   = '\0';
        if (c == ' ')
        {
            last_space = len - 1;
        }

        while (thefly_display.textWidth(line) > width && len > 0 && can_draw)
        {
            if (last_space != kNoSpace && last_space > 0)
            {
                char remainder[kLineMax] = {};
                strncpy(remainder, line + last_space + 1, sizeof(remainder) - 1);

                line[last_space] = '\0';
                size_t draw_len  = last_space;
                can_draw         = draw_line(line, draw_len, x, y, maxY, lineHeight);

                strncpy(line, remainder, sizeof(line) - 1);
                line[sizeof(line) - 1] = '\0';
                len                    = strlen(line);
                recalc_last_space(line, len, last_space);
            }
            else if (len > 1)
            {
                const char overflow = line[len - 1];
                line[len - 1]       = '\0';
                size_t draw_len     = len - 1;
                can_draw            = draw_line(line, draw_len, x, y, maxY, lineHeight);

                line[0]    = overflow;
                line[1]    = '\0';
                len        = 1;
                last_space = overflow == ' ' ? 0 : kNoSpace;
            }
            else
            {
                can_draw   = draw_line(line, len, x, y, maxY, lineHeight);
                last_space = kNoSpace;
            }
        }
    }

    if (can_draw && len > 0)
    {
        draw_line(line, len, x, y, maxY, lineHeight);
    }
}
} // namespace FlyGuiTextUtil

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

FlyGuiText::FlyGuiText(int16_t     x,
                       int16_t     y,
                       int16_t     width,
                       int16_t     height,
                       float       fontSize,
                       uint8_t     fontStyle,
                       size_t      maxLength,
                       const char* initialText)
    : FlyGuiItem(x, y, width, height, initialText), fontSize_(fontSize), fontStyle_(fontStyle), maxLength_(maxLength)
{
    text_      = static_cast<char*>(calloc(maxLength_ + 1, sizeof(char)));
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

void FlyGuiText::setTextColor(uint16_t color)
{
    if (textColor_ == color)
    {
        return;
    }

    textColor_ = color;
    colorDirty_ = true;
    setDirty();
}

void FlyGuiText::setClearOnUpdate(bool clearOnUpdate)
{
    clearOnUpdate_ = clearOnUpdate;
    setDirty();
}

bool FlyGuiText::redraw(bool forced)
{
    if (!visible() || (!forced && !dirty()))
    {
        return false;
    }

    thefly_display.setTextSize(fontSize_);
    thefly_display.setTextFont(fontStyle_);
    thefly_display.setTextColor(textColor_, TFT_BLACK);
    thefly_display.setTextDatum(top_left);

    const bool multiline = hasNewline(text_) || hasNewline(drawnText_);
    if (forced || clearOnUpdate_ || colorDirty_ || multiline)
    {
        thefly_display.fillRect(x(), y(), width(), height(), TFT_BLACK);
        drawTextBlock();
        updateRememberedText();
        colorDirty_ = false;
        markClean();
        return true;
    }

    // Design: FlyGuiText redraws only character ranges that changed.
    bool         drawn       = false;
    size_t       start       = 0;
    const size_t oldLength   = strlen(drawnText_);
    const size_t newLength   = strlen(text_);
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

        drawTextRun(start, end);
        drawn = true;
        start = end;
    }

    updateRememberedText();
    colorDirty_ = false;
    markClean();
    return drawn;
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void FlyGuiText::updateRememberedText()
{
    if (!drawnText_ || !text_)
    {
        return;
    }

    strncpy(drawnText_, text_, maxLength_);
    drawnText_[maxLength_] = '\0';
}

void FlyGuiText::drawTextBlock()
{
    if (!text_)
    {
        return;
    }

    int32_t lineY = y();
    char*   start = text_;
    while (start && *start != '\0')
    {
        char* end = start;
        while (*end != '\0' && *end != '\r' && *end != '\n')
        {
            ++end;
        }

        const char saved = *end;
        *end             = '\0';
        if (*start != '\0')
        {
            thefly_display.drawString(start, x(), lineY);
        }
        *end = saved;

        if (saved == '\0')
        {
            break;
        }

        lineY += lineHeight();
        if (lineY >= y() + height())
        {
            break;
        }

        start = end + 1;
        if (saved == '\r' && *start == '\n')
        {
            ++start;
        }
    }
}

void FlyGuiText::drawTextRun(size_t start, size_t end)
{
    const int32_t originX    = x() + textPrefixWidth(text_, start);
    const int32_t eraseWidth = textPrefixWidth(drawnText_, end) - textPrefixWidth(drawnText_, start);
    const int32_t drawWidth  = textPrefixWidth(text_, end) - textPrefixWidth(text_, start);
    const int32_t clearWidth = eraseWidth > drawWidth ? eraseWidth : drawWidth;

    thefly_display.fillRect(originX, y(), clearWidth + 2, textHeight(), TFT_BLACK);

    char saved = text_[end];
    text_[end] = '\0';
    thefly_display.drawString(text_ + start, originX, y());
    text_[end] = saved;
}

int32_t FlyGuiText::textPrefixWidth(const char* text, size_t length) const
{
    if (!text || length == 0)
    {
        return 0;
    }

    char saved                      = const_cast<char*>(text)[length];
    const_cast<char*>(text)[length] = '\0';
    const int32_t width             = thefly_display.textWidth(text);
    const_cast<char*>(text)[length] = saved;
    return width;
}

int32_t FlyGuiText::textHeight() const
{
    return height() > 0 ? height() : static_cast<int32_t>(fontSize_ * 8.0f) + 4;
}

int32_t FlyGuiText::lineHeight() const
{
    const int32_t height = thefly_display.fontHeight();
    return height > 0 ? height : static_cast<int32_t>(fontSize_ * 8.0f) + 4;
}

bool FlyGuiText::hasNewline(const char* text) const
{
    return text && (strchr(text, '\r') || strchr(text, '\n'));
}

FlyGuiDateTime::FlyGuiDateTime(int16_t x, int16_t y, int16_t width, int16_t height, float fontSize, uint8_t fontStyle)
    : FlyGuiText(x, y, width, height, fontSize, fontStyle, 31)
{
}

bool FlyGuiDateTime::redraw(bool forced)
{
    // Design: FlyGuiDateTime always shows current date/time and keeps frequent draws quick.
    const m5::rtc_datetime_t now = Clock.getDateTime();
    const char*              suffix =
                                    #ifdef ENABLE_POWER_KEY_INDICATOR
                                    BattTracker::powerKeyIndicatorActive() ? " ZZZ?" :
                                    #endif
                                    "";
    char                     text[32];
    if (kFlyGuiDebugClock)
    {
        snprintf(text,
                 sizeof(text),
                 "DEBUG-%02d-%02d %02d:%02d:%02d%s",
                 static_cast<int>(now.date.month),
                 static_cast<int>(now.date.date),
                 static_cast<int>(now.time.hours),
                 static_cast<int>(now.time.minutes),
                 static_cast<int>(now.time.seconds),
                 suffix);
    }
    else
    {
        snprintf(text,
                 sizeof(text),
                 "%04d-%02d-%02d %02d:%02d:%02d%s",
                 static_cast<int>(now.date.year),
                 static_cast<int>(now.date.month),
                 static_cast<int>(now.date.date),
                 static_cast<int>(now.time.hours),
                 static_cast<int>(now.time.minutes),
                 static_cast<int>(now.time.seconds),
                 suffix);
    }
    setText(text);
    return FlyGuiText::redraw(forced);
}

FlyGuiStopwatch::FlyGuiStopwatch(int16_t x, int16_t y, int16_t width, int16_t height, float fontSize, uint8_t fontStyle)
    : FlyGuiText(x, y, width, height, fontSize, fontStyle, 12), startMs_(millis())
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

bool FlyGuiStopwatch::redraw(bool forced)
{
    // Design: FlyGuiStopwatch shows elapsed time since a starting time.
    const uint32_t elapsed = elapsedMs() / 1000;
    const uint32_t hours   = elapsed / 3600;
    const uint32_t minutes = (elapsed / 60) % 60;
    const uint32_t seconds = elapsed % 60;

    char text[13];
    snprintf(text,
             sizeof(text),
             "%02lu:%02lu:%02lu",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
    setText(text);
    return FlyGuiText::redraw(forced);
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

namespace FlyGuiTextUtil
{
namespace
{
void trim_leading_spaces(char* line, size_t& len)
{
    size_t first = 0;
    while (first < len && line[first] == ' ')
    {
        ++first;
    }

    if (first == 0)
    {
        return;
    }

    memmove(line, line + first, len - first);
    len -= first;
    line[len] = '\0';
}

void recalc_last_space(const char* line, size_t len, size_t& last_space)
{
    last_space = kNoSpace;
    for (size_t i = 0; i < len; ++i)
    {
        if (line[i] == ' ')
        {
            last_space = i;
        }
    }
}

bool draw_line(char* line, size_t& len, int16_t x, int16_t& y, int16_t max_y, int16_t line_height)
{
    trim_leading_spaces(line, len);
    while (len > 0 && line[len - 1] == ' ')
    {
        line[--len] = '\0';
    }

    if (len == 0)
    {
        return true;
    }

    if (y + line_height > max_y)
    {
        return false;
    }

    thefly_display.drawString(line, x, y);
    y += line_height;
    len     = 0;
    line[0] = '\0';
    return true;
}
} // namespace
} // namespace FlyGuiTextUtil
