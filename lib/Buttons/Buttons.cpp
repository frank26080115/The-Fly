#include "Buttons.h"
#include <M5Unified.h>

#ifndef BTN_DEBOUNCE
#define BTN_DEBOUNCE 50
#endif

Button* buttons[BUTTONS_MAX_CNT] = {};

static inline uint32_t nonZeroMillis()
{
    uint32_t now = millis();
    return now == 0 ? 1 : now;
}

static void IRAM_ATTR gpioButtonIsr(void* arg)
{
    static_cast<GpioButton*>(arg)->handleInterrupt();
}

Button::Button(int idx, void(*app_fptr)(void)) :
    app_fptr(app_fptr),
    internal_index(static_cast<uint8_t>(idx))
{
    if (idx >= 0 && idx < BUTTONS_MAX_CNT)
    {
        buttons[idx] = this;
    }
}

void Button::poll()
{
    uint32_t now = nonZeroMillis();
    bool pressed = isPressed();

    if (pressed)
    {
        if (down_time == 0)
        {
            recordPress(now, true);
        }
    }
    else
    {
        down_time = 0;
    }
}

bool Button::hasPressed()
{
    return press_cnt != press_cnt_prev;
}

void Button::clrPressed()
{
    clr_time = nonZeroMillis();
    press_cnt_prev = press_cnt;
}

bool Button::isPressed()
{
    return false;
}

uint32_t Button::isHeld()
{
    return down_time == 0 ? 0 : millis() - down_time;
}

bool Button::hasIsrHandler()
{
    return false;
}

void Button::recordPress(uint32_t now, bool call_app_handler)
{
    if ((now - down_time) > BTN_DEBOUNCE && (now - clr_time) > BTN_DEBOUNCE)
    {
        ++press_cnt;
        if (call_app_handler)
        {
            dispatchAppHandler();
        }
    }
    down_time = now;
}

void Button::dispatchAppHandler()
{
    if (app_fptr != nullptr && app_press_cnt_prev != press_cnt)
    {
        app_press_cnt_prev = press_cnt;
        app_fptr();
    }
}

GpioButton::GpioButton(int pin, int idx, uint8_t down_state, void(*isr_fptr)(void), void(*app_fptr)(void)) :
    Button(idx, app_fptr),
    isr_fptr(isr_fptr),
    pin(static_cast<int8_t>(pin)),
    down_state(down_state)
{
    pinMode(this->pin, down_state == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    if (isr_fptr != nullptr)
    {
        attachInterruptArg(digitalPinToInterrupt(this->pin), gpioButtonIsr, this, down_state == LOW ? FALLING : RISING);
    }
}

void GpioButton::poll()
{
    uint32_t now = nonZeroMillis();
    bool pressed = isPressed();

    if (!pressed)
    {
        down_time = 0;
        return;
    }

    if (!hasIsrHandler() && down_time == 0)
    {
        recordPress(now, true);
        return;
    }

    dispatchAppHandler();

#ifdef TRY_CATCH_MISSED_GPIO_ISR
    if (down_time == 0 && (now - clr_time) > 100)
    {
        recordPress(now, true);
    }
#endif
}

bool GpioButton::hasPressed()
{
    return Button::hasPressed();
}

void GpioButton::clrPressed()
{
    Button::clrPressed();
}

uint32_t GpioButton::isHeld()
{
    return Button::isHeld();
}

void GpioButton::handleInterrupt()
{
    if (!isPressed())
    {
        return;
    }

    uint32_t now = nonZeroMillis();
    recordPress(now, false);
    if (isr_fptr != nullptr)
    {
        isr_fptr();
    }
}

TouchButton::TouchButton(int id, int idx, void(*app_fptr)(void)) :
    Button(idx, app_fptr),
    id(id)
{
}

void TouchButton::poll()
{
    Button::poll();
}

bool TouchButton::isPressed()
{
    switch (id)
    {
    case TOUCHBUTTON_LEFT:
        return M5.BtnA.isPressed();
    case TOUCHBUTTON_CENTER:
        return M5.BtnB.isPressed();
    case TOUCHBUTTON_RIGHT:
        return M5.BtnC.isPressed();
    default:
        return false;
    }
}

PwrButton::PwrButton(int idx, void(*app_fptr)(void)) :
    Button(idx, app_fptr)
{
}

void PwrButton::poll()
{
    Button::poll();
}

bool PwrButton::isPressed()
{
    return M5.BtnPWR.isPressed();
}

void buttons_init(void)
{
    for (uint8_t i = 0; i < BUTTONS_MAX_CNT; ++i)
    {
        if (buttons[i] != nullptr)
        {
            buttons[i]->clrPressed();
        }
    }
}

void buttons_poll(void)
{
    M5.update();
    for (uint8_t i = 0; i < BUTTONS_MAX_CNT; ++i)
    {
        if (buttons[i] != nullptr)
        {
            buttons[i]->poll();
        }
    }
}

bool buttons_anyPressed()
{
    for (uint8_t i = 0; i < BUTTONS_MAX_CNT; ++i)
    {
        if (buttons[i] != nullptr && buttons[i]->hasPressed())
        {
            return true;
        }
    }
    return false;
}

void buttons_clrAnyPressed()
{
    for (uint8_t i = 0; i < BUTTONS_MAX_CNT; ++i)
    {
        if (buttons[i] != nullptr)
        {
            buttons[i]->clrPressed();
        }
    }
}
