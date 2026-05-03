#include <Arduino.h>
#include "common.h"

TaskHandle_t loopTask_core0_Handle = NULL;
static void loopTask_core0(void *pvParameters);

void setup()
{
    xTaskCreateUniversal(loopTask_core0, "loopTask_core0", getArduinoLoopTaskStackSize(), NULL, 1, &loopTask_core0_Handle, 0);
}

void loop()
{
    // this is running on core 1
}

static void loopTask_core0(void *pvParameters)
{
    // this is running on core 0
    while (true)
    {
        yield();
    }
}
