#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum thefly_error_t
{
    // NOTE: we do not have an unknown type, we can't just... call an error dialog in the code, and not know what could cause it
    ERRTYPE_FATAL       = 0,  // hardware problem or bad build
    ERRTYPE_UNEXPECTED,       // invariant, what should be an assert, requires developer attention, things like failure to allocate memory
    ERRTYPE_REMOTE,           // caused by network conditions, disconnections
    ERRTYPE_USERCAUSED,       // caused by user, corrupted file stored on card, stuff like that
    ERRTYPE_AUTHENTICATION,   // authentication, only in context of web server and cloud activities
    ERRTYPE_FLAG_BLOCKING  = 0x80, // causes a tight loop within the error_f function
    ERRTYPE_FLAG_INVISIBLE = 0x40, // causes no GUI to show up, only serial port log
} thefly_error_t;

#define THEFLY_ERROR_AUTO_VIEW (-1)
#define THEFLY_ERROR_INVISIBLE (-2)

bool error_f(thefly_error_t type,
             int16_t next_view,
             const char* tag,
             const char* format_str,
             ...) __attribute__((format(printf, 4, 5)));

void show_fatal_error_f(bool fatal, const char* format_str, ...) __attribute__((format(printf, 2, 3)));

#define THEFLY_ERROR_WITH_FLAGS(type, flags) ((thefly_error_t)((type) | (flags)))

#define error_fatal_f(next_view, tag, format_str, ...) \
    error_f(ERRTYPE_FATAL, next_view, tag, format_str, ##__VA_ARGS__)

#define error_unexpected_f(next_view, tag, format_str, ...) \
    error_f(ERRTYPE_UNEXPECTED, next_view, tag, format_str, ##__VA_ARGS__)

#define error_remote_f(next_view, tag, format_str, ...) \
    error_f(ERRTYPE_REMOTE, next_view, tag, format_str, ##__VA_ARGS__)

#define error_usercaused_f(next_view, tag, format_str, ...) \
    error_f(ERRTYPE_USERCAUSED, next_view, tag, format_str, ##__VA_ARGS__)

#define error_authentication_f(next_view, tag, format_str, ...) \
    error_f(ERRTYPE_AUTHENTICATION, next_view, tag, format_str, ##__VA_ARGS__)

#define error_fatal_auto_f(tag, format_str, ...) \
    error_f(ERRTYPE_FATAL, THEFLY_ERROR_AUTO_VIEW, tag, format_str, ##__VA_ARGS__)

#define error_unexpected_auto_f(tag, format_str, ...) \
    error_f(ERRTYPE_UNEXPECTED, THEFLY_ERROR_AUTO_VIEW, tag, format_str, ##__VA_ARGS__)

#define error_remote_auto_f(tag, format_str, ...) \
    error_f(ERRTYPE_REMOTE, THEFLY_ERROR_AUTO_VIEW, tag, format_str, ##__VA_ARGS__)

#define error_usercaused_auto_f(tag, format_str, ...) \
    error_f(ERRTYPE_USERCAUSED, THEFLY_ERROR_AUTO_VIEW, tag, format_str, ##__VA_ARGS__)

#define error_authentication_auto_f(tag, format_str, ...) \
    error_f(ERRTYPE_AUTHENTICATION, THEFLY_ERROR_AUTO_VIEW, tag, format_str, ##__VA_ARGS__)

#define error_blocking_f(type, next_view, tag, format_str, ...) \
    error_f(THEFLY_ERROR_WITH_FLAGS(type, ERRTYPE_FLAG_BLOCKING), next_view, tag, format_str, ##__VA_ARGS__)

#define error_invisible_f(type, tag, format_str, ...) \
    error_f(THEFLY_ERROR_WITH_FLAGS(type, ERRTYPE_FLAG_INVISIBLE), THEFLY_ERROR_AUTO_VIEW, tag, format_str, ##__VA_ARGS__)

#define error_blocking_invisible_f(type, tag, format_str, ...) \
    error_f(THEFLY_ERROR_WITH_FLAGS(type, ERRTYPE_FLAG_BLOCKING | ERRTYPE_FLAG_INVISIBLE), THEFLY_ERROR_AUTO_VIEW, tag, format_str, ##__VA_ARGS__)

#define show_boot_error_f(fatal, format_str, ...) \
    error_f(THEFLY_ERROR_WITH_FLAGS((fatal) ? ERRTYPE_FATAL : ERRTYPE_UNEXPECTED, ERRTYPE_FLAG_BLOCKING), THEFLY_ERROR_AUTO_VIEW, ((const char*)0), format_str, ##__VA_ARGS__)
