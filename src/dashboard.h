#pragma once
#include <Arduino.h>
#include "epd_driver.h"
#include "firasans.h"
#include "firasans_small.h"

struct ClaudeStats {
    float fiveHourPct;      // 0-100, or -1 if unavailable
    char  fiveHourReset[32]; // e.g. "2h 15m"
    float sevenDayPct;      // 0-100, or -1 if unavailable
    char  sevenDayReset[32];
    float opusPct;          // 0-100, or -1 if unavailable
    float sonnetPct;        // 0-100, or -1 if unavailable
    char  plan[16];         // e.g. "Max"
    bool  valid;
};

void dashboard_init();
void dashboard_draw(const ClaudeStats &stats);
void dashboard_draw_waiting(const char *message);
