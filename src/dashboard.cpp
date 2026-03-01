#include "dashboard.h"
#include <cstdio>

extern uint8_t *framebuffer;

// Layout constants for 960x540 display
static const int SCREEN_W = 960;
static const int SCREEN_H = 540;
static const int MARGIN_X = 40;
static const int HEADER_Y = 60;
static const int BAR_START_Y = 120;
static const int BAR_HEIGHT = 30;
static const int BAR_SPACING = 70;
static const int BAR_WIDTH = 600;
static const int LABEL_X = MARGIN_X;
static const int BAR_X = MARGIN_X + 120;
static const int PCT_X = BAR_X + BAR_WIDTH + 20;

static void draw_bar(int x, int y, int width, int height, int percent) {
    // Draw outline
    epd_draw_rect(x, y, width, height, 0x00, framebuffer);
    // Draw fill
    int fill_w = (width - 4) * percent / 100;
    if (fill_w > 0) {
        // Use darker shade for higher values
        uint8_t shade = 0x00; // black fill
        epd_fill_rect(x + 2, y + 2, fill_w, height - 4, shade, framebuffer);
    }
}

void dashboard_init() {
    // framebuffer allocated in main
}

void dashboard_draw_waiting() {
    memset(framebuffer, 0xFF, SCREEN_W * SCREEN_H / 2);

    int32_t cx = SCREEN_W / 2 - 200;
    int32_t cy = SCREEN_H / 2;
    writeln((GFXfont *)&FiraSans, "Waiting for PC connection...", &cx, &cy, framebuffer);

    epd_poweron();
    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}

void dashboard_draw(const SystemStats &stats) {
    memset(framebuffer, 0xFF, SCREEN_W * SCREEN_H / 2);

    char buf[64];
    int32_t cx, cy;

    // Header
    cx = SCREEN_W / 2 - 120;
    cy = HEADER_Y;
    writeln((GFXfont *)&FiraSans, "PC STATS MONITOR", &cx, &cy, framebuffer);

    // Divider line
    epd_fill_rect(MARGIN_X, HEADER_Y + 10, SCREEN_W - 2 * MARGIN_X, 2, 0x00, framebuffer);

    // CPU
    int row = 0;
    int y = BAR_START_Y + row * BAR_SPACING;
    cx = LABEL_X; cy = y + 22;
    writeln((GFXfont *)&FiraSans, "CPU", &cx, &cy, framebuffer);
    draw_bar(BAR_X, y, BAR_WIDTH, BAR_HEIGHT, stats.cpu);
    snprintf(buf, sizeof(buf), "%d%%", stats.cpu);
    cx = PCT_X; cy = y + 22;
    writeln((GFXfont *)&FiraSans, buf, &cx, &cy, framebuffer);

    // RAM
    row = 1;
    y = BAR_START_Y + row * BAR_SPACING;
    cx = LABEL_X; cy = y + 22;
    writeln((GFXfont *)&FiraSans, "RAM", &cx, &cy, framebuffer);
    draw_bar(BAR_X, y, BAR_WIDTH, BAR_HEIGHT, stats.ram);
    snprintf(buf, sizeof(buf), "%d%%", stats.ram);
    cx = PCT_X; cy = y + 22;
    writeln((GFXfont *)&FiraSans, buf, &cx, &cy, framebuffer);

    // TEMP
    row = 2;
    y = BAR_START_Y + row * BAR_SPACING;
    cx = LABEL_X; cy = y + 22;
    writeln((GFXfont *)&FiraSans, "TMP", &cx, &cy, framebuffer);
    int temp_pct = constrain(stats.temp, 0, 100);
    draw_bar(BAR_X, y, BAR_WIDTH, BAR_HEIGHT, temp_pct);
    snprintf(buf, sizeof(buf), "%dC", stats.temp);
    cx = PCT_X; cy = y + 22;
    writeln((GFXfont *)&FiraSans, buf, &cx, &cy, framebuffer);

    // DISK
    row = 3;
    y = BAR_START_Y + row * BAR_SPACING;
    cx = LABEL_X; cy = y + 22;
    writeln((GFXfont *)&FiraSans, "DSK", &cx, &cy, framebuffer);
    draw_bar(BAR_X, y, BAR_WIDTH, BAR_HEIGHT, stats.disk);
    snprintf(buf, sizeof(buf), "%d%%", stats.disk);
    cx = PCT_X; cy = y + 22;
    writeln((GFXfont *)&FiraSans, buf, &cx, &cy, framebuffer);

    // NET
    row = 4;
    y = BAR_START_Y + row * BAR_SPACING;
    cx = LABEL_X; cy = y + 22;
    snprintf(buf, sizeof(buf), "NET  Up %.1f Mbps  Down %.1f Mbps", stats.netUp, stats.netDown);
    writeln((GFXfont *)&FiraSans, buf, &cx, &cy, framebuffer);

    // Timestamp
    cx = SCREEN_W / 2 - 100;
    cy = SCREEN_H - 30;
    unsigned long secs = millis() / 1000;
    int h = secs / 3600;
    int m = (secs % 3600) / 60;
    int s = secs % 60;
    snprintf(buf, sizeof(buf), "Uptime: %02d:%02d:%02d", h, m, s);
    writeln((GFXfont *)&FiraSans, buf, &cx, &cy, framebuffer);

    // Draw to screen
    epd_poweron();
    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}
