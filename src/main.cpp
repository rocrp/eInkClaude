#include <Arduino.h>
#include "epd_driver.h"
#include "firasans.h"

uint8_t *framebuffer = NULL;

void setup() {
    Serial.begin(115200);
    Serial.println("EPD Hello World");

    epd_init();

    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer) {
        Serial.println("framebuffer alloc failed!");
        while (1);
    }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    epd_poweron();
    epd_clear();

    int32_t cursor_x = 100;
    int32_t cursor_y = 270;
    writeln((GFXfont *)&FiraSans, "Hello from PC Stats Dashboard!", &cursor_x, &cursor_y, framebuffer);
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);

    epd_poweroff();
}

void loop() {
    delay(1000);
}
