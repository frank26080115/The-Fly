#include "error_report.h"

#include <Arduino.h>
#include "BattTracker.h"
#include "ClockAgent.h"
#include "ErrorView.h"
#include "FlyGui.h"
#include "ModalDialog.h"
#include "WifiManager.h"
#include "sprites.h"
#include <stdarg.h>
#include <stdio.h>

extern FlyGui* gui;
extern WifiManager* wifi_manager;
extern ErrorView* get_view_error();
extern ModalDialog* get_view_modal_dialog();

namespace
{
constexpr size_t kErrorTextMax = 255;
constexpr uint8_t kErrorFlagMask = ERRTYPE_FLAG_BLOCKING | ERRTYPE_FLAG_INVISIBLE;
constexpr uint8_t kErrorTypeMask = static_cast<uint8_t>(~kErrorFlagMask);

thefly_error_t error_base_type(thefly_error_t type)
{
    return static_cast<thefly_error_t>(static_cast<uint8_t>(type) & kErrorTypeMask);
}

bool error_has_flag(thefly_error_t type, thefly_error_t flag)
{
    return (static_cast<uint8_t>(type) & static_cast<uint8_t>(flag)) != 0;
}

const char* error_type_name(thefly_error_t type)
{
    switch (error_base_type(type))
    {
    case ERRTYPE_FATAL:
        return "FATAL";
    case ERRTYPE_UNEXPECTED:
        return "UNEXPECTED";
    case ERRTYPE_REMOTE:
        return "REMOTE";
    case ERRTYPE_USERCAUSED:
        return "USERCAUSED";
    case ERRTYPE_AUTHENTICATION:
        return "AUTHENTICATION";
    default:
        return "UNKNOWN";
    }
}

void error_time_text(char* out, size_t out_size)
{
    if (!out || out_size == 0)
    {
        return;
    }

    if (Clock.isSynced())
    {
        m5::rtc_datetime_t datetime = {};
        if (Clock.getDateTime(&datetime))
        {
            snprintf(out,
                     out_size,
                     "%04d-%02d-%02d %02d:%02d:%02d",
                     static_cast<int>(datetime.date.year),
                     static_cast<int>(datetime.date.month),
                     static_cast<int>(datetime.date.date),
                     static_cast<int>(datetime.time.hours),
                     static_cast<int>(datetime.time.minutes),
                     static_cast<int>(datetime.time.seconds));
            return;
        }
    }

    snprintf(out, out_size, "+%lu ms", static_cast<unsigned long>(millis()));
}

uint16_t automatic_next_view()
{
    if (!gui || !gui->currentView())
    {
        return FLYGUI_VIEW_MAIN;
    }

    const uint16_t current = gui->currentView()->id();
    if (current == FLYGUI_VIEW_SPLASH || current == FLYGUI_VIEW_ERROR || current == FLYGUI_VIEW_MODAL_DIALOG)
    {
        return FLYGUI_VIEW_MAIN;
    }

    return current;
}

void print_serial_error(thefly_error_t type, const char* tag, const char* message)
{
    char time_text[32] = {};
    error_time_text(time_text, sizeof(time_text));

    if (tag && tag[0] != '\0')
    {
        Serial.printf("[%s] [%s] [%s] %s\n", time_text, error_type_name(type), tag, message ? message : "");
    }
    else
    {
        Serial.printf("[%s] [%s] %s\n", time_text, error_type_name(type), message ? message : "");
    }
    Serial.flush();
}

bool show_error_view(thefly_error_t type, bool blocking, const char* message)
{
    const thefly_error_t base_type = error_base_type(type);
    ErrorView* view = get_view_error();
    if (!gui || !view)
    {
        if (base_type == ERRTYPE_FATAL)
        {
            while (true)
            {
                delay(1000);
            }
        }
        return false;
    }

    const uint16_t previous_view_id = automatic_next_view();
    const bool     fatal = base_type == ERRTYPE_FATAL;

    view->setMessage(message, fatal);
    if (!gui->showView(FLYGUI_VIEW_ERROR))
    {
        if (fatal)
        {
            while (true)
            {
                delay(1000);
            }
        }
        return false;
    }

    gui->redraw(true);
    if (!blocking && !fatal)
    {
        return true;
    }

    while (fatal || !view->dismissed())
    {
        gui->poll();
        BattTracker::poll();
        if (wifi_manager)
        {
            wifi_manager->poll();
        }
        delay(10);
    }

    if (previous_view_id != FLYGUI_VIEW_ERROR)
    {
        gui->showView(previous_view_id);
    }

    return true;
}

bool show_error_modal(int16_t next_view, const char* message)
{
    ModalDialog* dialog = get_view_modal_dialog();
    if (!gui || !dialog)
    {
        return false;
    }

    const uint16_t dismiss_view = next_view < 0 ? automatic_next_view() : static_cast<uint16_t>(next_view);
    dialog->configure(sprite_warning_100,
                      SPRITE_WARNING_100_BYTES,
                      SPRITE_WARNING_100_WIDTH,
                      SPRITE_WARNING_100_HEIGHT,
                      message ? message : "",
                      dismiss_view);
    return gui->showView(FLYGUI_VIEW_MODAL_DIALOG);
}

bool error_v(thefly_error_t type, int16_t next_view, const char* tag, const char* format_str, va_list args)
{
    char message[kErrorTextMax + 1] = {};
    vsnprintf(message, sizeof(message), format_str ? format_str : "", args);

    print_serial_error(type, tag, message);

    if (error_has_flag(type, ERRTYPE_FLAG_INVISIBLE) || next_view == THEFLY_ERROR_INVISIBLE)
    {
        return true;
    }

    const thefly_error_t base_type = error_base_type(type);
    const bool effective_blocking = error_has_flag(type, ERRTYPE_FLAG_BLOCKING) || base_type == ERRTYPE_FATAL;
    if (effective_blocking)
    {
        return show_error_view(type, true, message);
    }

    return show_error_modal(next_view, message);
}

} // namespace

bool error_f(thefly_error_t type, int16_t next_view, const char* tag, const char* format_str, ...)
{
    va_list args;
    va_start(args, format_str);
    const bool shown = error_v(type, next_view, tag, format_str, args);
    va_end(args);
    return shown;
}

void show_fatal_error_f(bool fatal, const char* format_str, ...)
{
    va_list args;
    va_start(args, format_str);
    const thefly_error_t type = static_cast<thefly_error_t>((fatal ? ERRTYPE_FATAL : ERRTYPE_UNEXPECTED) | ERRTYPE_FLAG_BLOCKING);
    error_v(type, THEFLY_ERROR_AUTO_VIEW, nullptr, format_str, args);
    va_end(args);
}
