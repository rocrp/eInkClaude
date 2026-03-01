#include "dashboard.h"
#include <cstdio>
#include <cstring>

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

// Number of data rows (CPU, RAM, TMP, DSK, NET)
static const int NUM_ROWS = 5;

// Row regions: each row spans full display width, covering its vertical band.
// This allows passing framebuffer + y_offset directly to epd_draw_grayscale_image
// because with area.width == EPD_WIDTH, the driver uses EPD_WIDTH/2 stride which
// matches the full framebuffer layout.
static Rect_t row_region(int row_index) {
    int y = BAR_START_Y + row_index * BAR_SPACING;
    // Extend a few pixels above for text ascenders and below for descenders
    int y0 = y - 5;
    int h = BAR_SPACING;
    // Clamp to screen
    if (y0 < 0) y0 = 0;
    if (y0 + h > SCREEN_H) h = SCREEN_H - y0;
    return {0, y0, SCREEN_W, h};
}

// Uptime region at the bottom of the screen
static Rect_t uptime_region() {
    int y0 = SCREEN_H - 55;
    int h = 55;
    return {0, y0, SCREEN_W, h};
}

// Previous stats for change detection
static SystemStats prev_stats = {};
static bool first_draw = true;

// Clear a region in the framebuffer (set to white = 0xFF)
static void clear_fb_region(const Rect_t &r) {
    for (int y = r.y; y < r.y + r.height && y < SCREEN_H; y++) {
        memset(&framebuffer[y * SCREEN_W / 2 + r.x / 2], 0xFF, r.width / 2);
    }
}

// Push a full-width region from framebuffer to the e-paper display.
// The data pointer is offset to the start of the region's first row.
static void push_region(const Rect_t &r) {
    uint8_t *data_ptr = framebuffer + r.y * (SCREEN_W / 2);
    epd_clear_area(r);
    epd_draw_grayscale_image(r, data_ptr);
}

static void draw_bar(int x, int y, int width, int height, int percent) {
    // Draw outline
    epd_draw_rect(x, y, width, height, 0x00, framebuffer);
    // Draw fill
    int fill_w = (width - 4) * percent / 100;
    if (fill_w > 0) {
        uint8_t shade = 0x00; // black fill
        epd_fill_rect(x + 2, y + 2, fill_w, height - 4, shade, framebuffer);
    }
}

// Draw a single stat row (label + bar + value text) into the framebuffer
static void draw_row_cpu(const SystemStats &stats) {
    int row = 0;
    int y = BAR_START_Y + row * BAR_SPACING;
    int32_t cx = LABEL_X, cy = y + 22;
    writeln((GFXfont *)&FiraSans, "CPU", &cx, &cy, framebuffer);
    draw_bar(BAR_X, y, BAR_WIDTH, BAR_HEIGHT, stats.cpu);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", stats.cpu);
    cx = PCT_X; cy = y + 22;
    writeln((GFXfont *)&FiraSans, buf, &cx, &cy, framebuffer);
}

static void draw_row_ram(const SystemStats &stats) {
    int row = 1;
    int y = BAR_START_Y + row * BAR_SPACING;
    int32_t cx = LABEL_X, cy = y + 22;
    writeln((GFXfont *)&FiraSans, "RAM", &cx, &cy, framebuffer);
    draw_bar(BAR_X, y, BAR_WIDTH, BAR_HEIGHT, stats.ram);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", stats.ram);
    cx = PCT_X; cy = y + 22;
    writeln((GFXfont *)&FiraSans, buf, &cx, &cy, framebuffer);
}

static void draw_row_tmp(const SystemStats &stats) {
    int row = 2;
    int y = BAR_START_Y + row * BAR_SPACING;
    int32_t cx = LABEL_X, cy = y + 22;
    writeln((GFXfont *)&FiraSans, "TMP", &cx, &cy, framebuffer);
    int temp_pct = constrain(stats.temp, 0, 100);
    draw_bar(BAR_X, y, BAR_WIDTH, BAR_HEIGHT, temp_pct);
    char buf[32];
    snprintf(buf, sizeof(buf), "%dC", stats.temp);
    cx = PCT_X; cy = y + 22;
    writeln((GFXfont *)&FiraSans, buf, &cx, &cy, framebuffer);
}

static void draw_row_dsk(const SystemStats &stats) {
    int row = 3;
    int y = BAR_START_Y + row * BAR_SPACING;
    int32_t cx = LABEL_X, cy = y + 22;
    writeln((GFXfont *)&FiraSans, "DSK", &cx, &cy, framebuffer);
    draw_bar(BAR_X, y, BAR_WIDTH, BAR_HEIGHT, stats.disk);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", stats.disk);
    cx = PCT_X; cy = y + 22;
    writeln((GFXfont *)&FiraSans, buf, &cx, &cy, framebuffer);
}

static void draw_row_net(const SystemStats &stats) {
    int row = 4;
    int y = BAR_START_Y + row * BAR_SPACING;
    int32_t cx = LABEL_X, cy = y + 22;
    char buf[64];
    snprintf(buf, sizeof(buf), "NET  Up %.1f Mbps  Down %.1f Mbps", stats.netUp, stats.netDown);
    writeln((GFXfont *)&FiraSans, buf, &cx, &cy, framebuffer);
}

static void draw_uptime() {
    int32_t cx = SCREEN_W / 2 - 100;
    int32_t cy = SCREEN_H - 30;
    unsigned long secs = millis() / 1000;
    int h = secs / 3600;
    int m = (secs % 3600) / 60;
    int s = secs % 60;
    char buf[64];
    snprintf(buf, sizeof(buf), "Uptime: %02d:%02d:%02d", h, m, s);
    writeln((GFXfont *)&FiraSans, buf, &cx, &cy, framebuffer);
}

void dashboard_init() {
    first_draw = true;
    memset(&prev_stats, 0, sizeof(prev_stats));
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

    // Reset state so next dashboard_draw does a full render
    first_draw = true;
}

void dashboard_draw(const SystemStats &stats) {
    if (first_draw) {
        // ---- FIRST DRAW: full clear + full redraw ----
        memset(framebuffer, 0xFF, SCREEN_W * SCREEN_H / 2);

        // Header
        int32_t cx = SCREEN_W / 2 - 120;
        int32_t cy = HEADER_Y;
        writeln((GFXfont *)&FiraSans, "PC STATS MONITOR", &cx, &cy, framebuffer);

        // Divider line
        epd_fill_rect(MARGIN_X, HEADER_Y + 10, SCREEN_W - 2 * MARGIN_X, 2, 0x00, framebuffer);

        // All rows
        draw_row_cpu(stats);
        draw_row_ram(stats);
        draw_row_tmp(stats);
        draw_row_dsk(stats);
        draw_row_net(stats);
        draw_uptime();

        // Full screen push
        epd_poweron();
        epd_clear();
        epd_draw_grayscale_image(epd_full_screen(), framebuffer);
        epd_poweroff();

        prev_stats = stats;
        first_draw = false;
        return;
    }

    // ---- SUBSEQUENT DRAWS: partial refresh for changed rows only ----

    // Determine which rows changed
    bool cpu_changed = (stats.cpu != prev_stats.cpu);
    bool ram_changed = (stats.ram != prev_stats.ram);
    bool tmp_changed = (stats.temp != prev_stats.temp);
    bool dsk_changed = (stats.disk != prev_stats.disk);
    bool net_changed = (stats.netUp != prev_stats.netUp || stats.netDown != prev_stats.netDown);
    // Uptime always changes
    bool uptime_changed = true;

    // If nothing changed at all (except uptime), we still update uptime
    bool any_changed = cpu_changed || ram_changed || tmp_changed || dsk_changed || net_changed || uptime_changed;
    if (!any_changed) {
        prev_stats = stats;
        return;
    }

    epd_poweron();

    if (cpu_changed) {
        Rect_t r = row_region(0);
        clear_fb_region(r);
        draw_row_cpu(stats);
        push_region(r);
    }

    if (ram_changed) {
        Rect_t r = row_region(1);
        clear_fb_region(r);
        draw_row_ram(stats);
        push_region(r);
    }

    if (tmp_changed) {
        Rect_t r = row_region(2);
        clear_fb_region(r);
        draw_row_tmp(stats);
        push_region(r);
    }

    if (dsk_changed) {
        Rect_t r = row_region(3);
        clear_fb_region(r);
        draw_row_dsk(stats);
        push_region(r);
    }

    if (net_changed) {
        Rect_t r = row_region(4);
        clear_fb_region(r);
        draw_row_net(stats);
        push_region(r);
    }

    if (uptime_changed) {
        Rect_t r = uptime_region();
        clear_fb_region(r);
        draw_uptime();
        push_region(r);
    }

    epd_poweroff();

    prev_stats = stats;
}
