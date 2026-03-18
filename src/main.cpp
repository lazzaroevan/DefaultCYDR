#include <Arduino.h>
#include <lvgl.h>
#include <esp32_smartdisplay.h>
#include "ui/ui.h"

void setup() {
    Serial.begin(115200);
    smartdisplay_init();
    smartdisplay_lcd_set_backlight(1.0f);
    ui_init();
}

void loop() {
    lv_timer_handler();
    delay(5);
}