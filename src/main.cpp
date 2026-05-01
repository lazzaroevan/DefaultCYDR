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

static bool foundLight = false;
static int count = 0;

//bluetooth
#include <NimBLEDevice.h>

static NimBLEUUID serviceUUID("0000fff0-0000-1000-8000-00805f9b34fb");
static NimBLEUUID charUUID("0000fff3-0000-1000-8000-00805f9b34fb");
static std::string targetAddress = "BE:96:71:00:02:AD";

static bool shouldConnect = true;
static bool isConnected = false;

NimBLEClient* pClient = nullptr;

lv_timer_t * conn_timer;
int retry_count = 0;
//const int MAX_RETRIES = 5;

/* Store the selected color here */
uint8_t selected_r = 0;
uint8_t selected_g = 0;
uint8_t selected_b = 0;

//spinner for connection
lv_obj_t *spinner;

void bleConnectTask(void * pvParameters) {
    for(;;) {
        if(shouldConnect) {
            shouldConnect = false;
            Serial.println("Task: Attempting connection...");
            
            if (pClient == nullptr) {
                pClient = NimBLEDevice::createClient();
            }

            // This blocks THIS task, but keeps LVGL running on Core 1
            if (pClient->connect(NimBLEAddress(targetAddress, BLE_ADDR_PUBLIC))) {
                Serial.println("Task: Connected!");
                isConnected = true; 
            } else {
                Serial.println("Task: Connection failed.");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}



// Create client ONLY if it doesn't exist
    

bool ensureConnected() {
    // If already connected, do nothing
    if (pClient != nullptr && pClient->isConnected()) {
        Serial.println("Connected!");
        return true;
    }

    if (pClient == nullptr) {
        pClient = NimBLEDevice::createClient();
    }
    if (shouldConnect){
        Serial.println("Still trying to connect...");
    }else{
        Serial.println("Updating so now we look to connect");
        shouldConnect = true;
    }
    return false;
}

bool sendDataToLight(uint8_t* data, size_t dataSize) {
    if (!ensureConnected()) return false;

    NimBLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        Serial.println("Error: Service UUID not found on this device!");
        return false;
    }

    NimBLERemoteCharacteristic* pRemoteChar = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteChar == nullptr) {
        Serial.println("Error: Characteristic UUID not found in this service!");
        return false;
    }
    Serial.println("Sending data to light with write response...");
    pRemoteChar->writeValue(data, dataSize, true);
    Serial.println("Sending data to light with no response...");
    pRemoteChar->writeValue(data, dataSize, false);
    Serial.println("Data sent successfully!");
    return true;
}

bool setBrightness(uint8_t level) {
    uint8_t pkt[9] = {0x7E, 0x04, 0x01, level, 0x01, 0xFF, 0xFF, 0x00, 0xEF};
    Serial.println("Setting brightness to " + String(level));
    return sendDataToLight(pkt, sizeof(pkt));
}

bool powerOn() {
    uint8_t pkt[9] = {0x7E, 0x04, 0x04, 0xF0, 0x00, 0x01, 0xFF, 0x00, 0xEF};
    Serial.println("Powering on the light");
    return sendDataToLight(pkt, sizeof(pkt));
}

bool powerOff() {
  uint8_t pkt[9] = {0x7E, 0x04, 0x04, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xEF};
  Serial.println("Powering off the light");
  return sendDataToLight(pkt, sizeof(pkt));
}

bool setColor(uint8_t r, uint8_t g, uint8_t b) {
  //uint8_t pkt[9] = {0x7E, 0x07, 0x05, 0x03, r, g, b, 0x10, 0xEF}; OLD
  uint8_t pkt[9] = {0x7E, 0x07, 0x05, 0x03, r, g, b, 0x10, 0xEF};
  Serial.println("Setting color to R:" + String(r) + " G:" + String(g) + " B:" + String(b));
  return sendDataToLight(pkt, sizeof(pkt));
}


void ble_connect_timer_cb(lv_timer_t * timer) {
    ensureConnected();    
}



static void rgb565_to_rgb888(uint16_t px, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = ((px >> 11) & 0x1F) << 3;
    *g = ((px >> 5)  & 0x3F) << 2;
    *b = ( px        & 0x1F) << 3;
}

static bool get_image_pixel_rgb(uint32_t x, uint32_t y, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if(x >= ui_img_rainbowgradient_png.header.w || y >= ui_img_rainbowgradient_png.header.h) {
        return false;
    }

#if LV_COLOR_DEPTH == 16
    const uint8_t *data = (const uint8_t *)ui_img_rainbowgradient_png.data;
    uint32_t index = (y * ui_img_rainbowgradient_png.header.w + x) * 2;

    uint16_t px = (uint16_t)data[index] | ((uint16_t)data[index + 1] << 8);
    rgb565_to_rgb888(px, r, g, b);
    return true;
#else
    return false;
#endif
}

static void color_picker_event(lv_event_t * e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);

    lv_indev_t *indev = lv_indev_active();
    if(indev == NULL) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    int32_t local_x = p.x - a.x1;
    int32_t local_y = p.y - a.y1;

    int32_t obj_w = lv_obj_get_width(obj);
    int32_t obj_h = lv_obj_get_height(obj);

    if(local_x < 0 || local_y < 0 || local_x >= obj_w || local_y >= obj_h) {
        return;
    }

    uint32_t img_x = ((uint32_t)local_x * ui_img_rainbowgradient_png.header.w) / (uint32_t)obj_w;
    uint32_t img_y = ((uint32_t)local_y * ui_img_rainbowgradient_png.header.h) / (uint32_t)obj_h;

    uint8_t r, g, b;
    if(get_image_pixel_rgb(img_x, img_y, &r, &g, &b)) {
        selected_r = r;
        selected_g = g;
        selected_b = b;

        Serial.printf("Picked color: R=%u G=%u B=%u\n", selected_r, selected_g, selected_b);
        setColor(selected_r, selected_g, selected_b);
    }
}

static void adjustingBrightness(lv_event_t * e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    setBrightness(uint8_t(lv_slider_get_value(ui_Screen1_Slider_Slider1)));
}


static uint32_t my_tick_cb(void)
{
    return (uint32_t)millis();
}

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
        data->continue_reading = false;
        return;
    }

    TS_Point p = touch.getPoint();

    if (p.z < 300) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = false;
        return;
    }

    int16_t x = map(p.x, 230, 3745, 0, screenWidth - 1);
    int16_t y = map(p.y, 200, 3865, 0, screenHeight - 1);

    x = constrain(x, 0, screenWidth - 1);
    y = constrain(y, 0, screenHeight - 1);

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
    data->continue_reading = false;
}

void setup()
{
    Serial.begin(115200);

    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    touch.begin(touchSPI);
    touch.setRotation(1);

    lv_init();
    lv_tick_set_cb(my_tick_cb);

    display = lv_display_create(screenWidth, screenHeight);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, drawBuf, NULL, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, my_flush_cb);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touch_read_cb);
    lv_indev_set_display(indev, display);

    ui_init();

    spinner = lv_spinner_create(ui_Screen_Screen1);
    lv_obj_set_width(spinner, 200);
    lv_obj_set_height(spinner, 200);
    lv_obj_center(spinner);

    lv_obj_add_event_cb(ui_Screen1_Panel_Panel1 , color_picker_event, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(ui_Screen1_Slider_Slider1 , adjustingBrightness, LV_EVENT_VALUE_CHANGED, NULL);

    NimBLEDevice::init("ESP32_Control");
    xTaskCreatePinnedToCore(bleConnectTask, "BLE_Task", 4096, NULL, 1, NULL, 0);
    // Create timer: calls callback every 5000ms (5 seconds)
    conn_timer = lv_timer_create(ble_connect_timer_cb, 5000, NULL);
    //lv_timer_set_repeat_count(conn_timer, MAX_RETRIES); // Optional auto-delete
}

void loop() {
    lv_timer_handler();

    // Check if the background task succeeded
    if (isConnected) {
        isConnected = false; // Reset flag
        if (spinner != NULL) {
            lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN); // Hide spinner safely
            Serial.println("UI: Light connected, hiding spinner.");
        }
    }
    
    delay(5);
}