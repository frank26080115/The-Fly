#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>
#include "thefly_common.h"

#define BUTTONS_MAX_CNT 16

enum
{
    TOUCHBUTTON_LEFT,
    TOUCHBUTTON_CENTER,
    TOUCHBUTTON_RIGHT,
};

class Button
{
public:
    Button(int idx, void (*app_fptr)(void));
    virtual ~Button() = default;
    virtual void     poll();
    virtual bool     hasPressed();
    virtual void     clrPressed();
    virtual bool     isPressed();
    virtual uint32_t isHeld(); // if it is held, return the how long since the initial press down event
    virtual bool     hasIsrHandler();
    inline bool      hasAppHandler()
    {
        return app_fptr != nullptr;
    };

protected:
    void recordPress(uint32_t now, bool call_app_handler);
    void dispatchAppHandler();
    void (*app_fptr)(void)               = nullptr;
    volatile uint32_t down_time          = 0;
    volatile uint32_t clr_time           = 0;
    volatile uint32_t press_cnt          = 0;
    volatile uint32_t press_cnt_prev     = 0;
    uint32_t          app_press_cnt_prev = 0;
    uint8_t           internal_index; // tracks which one it is within the global `buttons` array
};

class GpioButton : public Button
{
public:
    GpioButton(int pin, int idx, uint8_t down_state, void (*isr_fptr)(void), void (*app_fptr)(void));
    virtual void poll();
    virtual bool hasPressed();
    virtual void clrPressed();
    virtual bool isPressed()
    {
        return digitalRead(this->pin) == this->down_state;
    };
    virtual uint32_t isHeld(); // if it is held, return the how long since the initial press down event
    virtual bool     hasIsrHandler()
    {
        return isr_fptr != nullptr;
    };
    void handleInterrupt();

private:
    void (*isr_fptr)(void) = nullptr;
    int8_t  pin;
    uint8_t down_state;
};

class TouchButton : public Button
{
public:
    TouchButton(int id, int idx, void (*app_fptr)(void));
    virtual void poll();
    virtual bool isPressed();

private:
    int id;
};

class PwrButton : public Button
{
public:
    PwrButton(int idx, void (*app_fptr)(void));
    virtual void poll();
    virtual bool isPressed();
};

extern void buttons_init(void);
extern void buttons_poll(void);
extern bool buttons_anyPressed();
extern void buttons_clrAnyPressed();

extern Button* buttons[BUTTONS_MAX_CNT];
