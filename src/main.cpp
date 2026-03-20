#include <Arduino.h>
#include <SPI.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "ui/ui.h"

static TFT_eSPI tft = TFT_eSPI();

// Backlight
static constexpr int TFT_BL_PIN = 21;

// XPT2046 touch pins for CYD ESP32-2432S028R
static constexpr int TOUCH_CS   = 33;
static constexpr int TOUCH_IRQ  = 36;
static constexpr int TOUCH_CLK  = 25;
static constexpr int TOUCH_MISO = 39;
static constexpr int TOUCH_MOSI = 32;

// Display is running landscape
static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;

// Use a separate SPI object for touch
static SPIClass touchSPI(VSPI);
static XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

static lv_display_t *display;
static uint16_t drawBuf[screenWidth * 20];

// Adjust these after calibration
static int touchMinX = 230;
static int touchMaxX = 3745;
static int touchMinY = 200;
static int touchMaxY = 3865;

static inline void swap565_buffer(uint8_t *buf, uint32_t pixel_count)
{
    uint16_t *p = (uint16_t *)buf;
    for (uint32_t i = 0; i < pixel_count; i++) {
        uint16_t c = p[i];
        p[i] = (uint16_t)((c << 8) | (c >> 8));
    }
}

static void my_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    swap565_buffer(px_map, w * h);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px_map, w * h, false);
    tft.endWrite();

    lv_display_flush_ready(disp);
}

static void my_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void) indev;

        if (!touch.touched()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    TS_Point p = touch.getPoint();
    if (p.z < 300) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // Map raw touch to landscape screen coordinates.
    // These may need swapping/inverting depending on your panel orientation.
    int16_t x = map(p.x, touchMinX, touchMaxX, 0, screenWidth  - 1);
    int16_t y = map(p.y, touchMinY, touchMaxY, 0, screenHeight - 1);

    x = constrain(x, 0, screenWidth - 1);
    y = constrain(y, 0, screenHeight - 1);

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
}

void setup()
{
    Serial.begin(115200);

    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    // Touch SPI
    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    touch.begin(touchSPI);
    touch.setRotation(1);

    lv_init();

    display = lv_display_create(screenWidth, screenHeight);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, drawBuf, NULL, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, my_flush_cb);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touch_read_cb);

    ui_init();
}

void loop()
{
    lv_timer_handler();
    delay(5);

    if (touch.touched()) {
    TS_Point p = touch.getPoint();
    Serial.printf("raw x=%d y=%d z=%d\n", p.x, p.y, p.z);
    delay(100);
}
}