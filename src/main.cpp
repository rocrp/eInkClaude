#include <Arduino.h>
#include <ArduinoJson.h>
#include "epd_driver.h"
#include "dashboard.h"

uint8_t *framebuffer = NULL;

SystemStats currentStats = {0, 0, 0, 0, 0.0, 0.0, false};
String serialBuffer = "";
unsigned long lastUpdate = 0;
static const unsigned long UPDATE_INTERVAL = 5000; // min 5s between redraws
bool needsRedraw = false;

void parseStats(const String &line) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        Serial.print("JSON parse error: ");
        Serial.println(err.c_str());
        return;
    }

    currentStats.cpu = doc["cpu"] | 0;
    currentStats.ram = doc["ram"] | 0;
    currentStats.temp = doc["temp"] | 0;
    currentStats.disk = doc["disk"] | 0;
    currentStats.netUp = doc["net_up"] | 0.0f;
    currentStats.netDown = doc["net_down"] | 0.0f;
    currentStats.valid = true;
    needsRedraw = true;
}

void setup() {
    Serial.begin(115200);
    Serial.println("PC Stats Dashboard v1.0");

    epd_init();

    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer) {
        Serial.println("framebuffer alloc failed!");
        while (1);
    }

    dashboard_init();
    dashboard_draw_waiting();
}

void loop() {
    // Read serial data
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            serialBuffer.trim();
            if (serialBuffer.length() > 0) {
                parseStats(serialBuffer);
            }
            serialBuffer = "";
        } else {
            serialBuffer += c;
        }
    }

    // Redraw if new data and enough time has passed
    if (needsRedraw && currentStats.valid && (millis() - lastUpdate > UPDATE_INTERVAL)) {
        dashboard_draw(currentStats);
        lastUpdate = millis();
        needsRedraw = false;
        Serial.println("OK");
    }
}
