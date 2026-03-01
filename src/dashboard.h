#pragma once
#include <Arduino.h>
#include "epd_driver.h"
#include "firasans.h"

struct SystemStats {
    int cpu;        // 0-100
    int ram;        // 0-100
    int temp;       // Celsius
    int disk;       // 0-100
    float netUp;    // Mbps
    float netDown;  // Mbps
    bool valid;
};

void dashboard_init();
void dashboard_draw(const SystemStats &stats);
void dashboard_draw_waiting();
