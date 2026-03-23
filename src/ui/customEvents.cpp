#include <Arduino.h>
#include <lvgl.h>

extern "C" void mainButtonPressed(lv_event_t * e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        Serial.println("Button clicked");
    }
}