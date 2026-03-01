#include "dashboard.h"
#include <cstdio>
#include <cstring>
#include <time.h>

extern uint8_t *framebuffer;

// Layout constants for 960x540 display
static const int SCREEN_W = 960;
static const int SCREEN_H = 540;
static const int MARGIN_X = 40;
static const int HEADER_Y = 55;
static const int DIVIDER_Y = HEADER_Y + 12;
static const int FOOTER_H = 50;
static const int CONTENT_BOTTOM = SCREEN_H - FOOTER_H;
static const int BAR_HEIGHT = 30;
static const int BAR_WIDTH = 520;
// Center the row group: label(~100) + gap(70) + bar(520) + gap(20) + pct(~60) = ~770
static const int ROW_CONTENT_W = 770;
static const int LABEL_X = (SCREEN_W - ROW_CONTENT_W) / 2;
static const int BAR_X = LABEL_X + 170;
static const int PCT_X = BAR_X + BAR_WIDTH + 20;

// Max visible rows
static const int MAX_ROWS = 2;

// Row descriptor for dynamic layout
struct RowData {
    const char *label;
    float pct;
    char resetText[32];
};

// Previous state for change detection
static int prev_num_rows = 0;
static RowData prev_rows[MAX_ROWS] = {};
static bool first_draw = true;

// Computed layout (updated each draw for centering)
static int row_spacing = 120;
static int content_top = 100;

static Rect_t row_region(int row_index) {
    int y = content_top + row_index * row_spacing;
    int y0 = y - 8;
    int h = row_spacing;
    if (y0 < 0) y0 = 0;
    if (y0 + h > CONTENT_BOTTOM) h = CONTENT_BOTTOM - y0;
    return {0, y0, SCREEN_W, h};
}

static Rect_t footer_region() {
    int y0 = SCREEN_H - FOOTER_H;
    return {0, y0, SCREEN_W, FOOTER_H};
}

static void clear_fb_region(const Rect_t &r) {
    for (int y = r.y; y < r.y + r.height && y < SCREEN_H; y++) {
        memset(&framebuffer[y * SCREEN_W / 2 + r.x / 2], 0xFF, r.width / 2);
    }
}

static void push_region(const Rect_t &r) {
    uint8_t *data_ptr = framebuffer + r.y * (SCREEN_W / 2);
    epd_clear_area(r);
    epd_draw_grayscale_image(r, data_ptr);
}

static void draw_bar(int x, int y, int width, int height, int percent) {
    epd_draw_rect(x, y, width, height, 0x00, framebuffer);
    int fill_w = (width - 4) * percent / 100;
    if (fill_w > 0) {
        epd_fill_rect(x + 2, y + 2, fill_w, height - 4, 0x00, framebuffer);
    }
}

static void draw_usage_row(int row, const RowData &rd) {
    int y = content_top + row * row_spacing;

    // Label
    int32_t cx = LABEL_X, cy = y + 22;
    writeln((GFXfont *)&FiraSans, rd.label, &cx, &cy, framebuffer);

    // Bar + percentage
    int p = constrain((int)(rd.pct + 0.5f), 0, 100);
    draw_bar(BAR_X, y, BAR_WIDTH, BAR_HEIGHT, p);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", p);
    cx = PCT_X; cy = y + 22;
    writeln((GFXfont *)&FiraSans, buf, &cx, &cy, framebuffer);

    // Sub-line: reset countdown (shifted 10px down)
    if (rd.resetText[0] != '\0') {
        char sub[48];
        snprintf(sub, sizeof(sub), "Resets in %s", rd.resetText);
        cx = BAR_X; cy = y + 22 + 40;
        writeln((GFXfont *)&FiraSansSmall, sub, &cx, &cy, framebuffer);
    }
}

static void draw_header() {
    int32_t cx = SCREEN_W / 2 - 90;
    int32_t cy = HEADER_Y;
    writeln((GFXfont *)&FiraSans, "eInkClaude", &cx, &cy, framebuffer);
    epd_fill_rect(MARGIN_X, DIVIDER_Y, SCREEN_W - 2 * MARGIN_X, 2, 0x00, framebuffer);
}

static void draw_footer(const char *plan) {
    time_t now = time(nullptr);
    struct tm *ti = localtime(&now);
    int h = ti ? ti->tm_hour : 0;
    int m = ti ? ti->tm_min  : 0;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s Plan  |  Updated %02d:%02d", plan, h, m);
    int32_t x1, y1, tw, th;
    int32_t tx = 0, ty = 0;
    get_text_bounds((GFXfont *)&FiraSansSmall, buf, &tx, &ty, &x1, &y1, &tw, &th, NULL);
    int32_t cx = (SCREEN_W - tw) / 2;
    int32_t cy = SCREEN_H - 15;
    writeln((GFXfont *)&FiraSansSmall, buf, &cx, &cy, framebuffer);
}

// Build array of visible rows from stats, returns count
static int build_rows(const ClaudeStats &stats, RowData rows[MAX_ROWS]) {
    int n = 0;
    if (stats.fiveHourPct >= 0) {
        rows[n].label = "5 HOUR";
        rows[n].pct = stats.fiveHourPct;
        strlcpy(rows[n].resetText, stats.fiveHourReset, sizeof(rows[n].resetText));
        n++;
    }
    if (stats.sevenDayPct >= 0) {
        rows[n].label = "7 DAY";
        rows[n].pct = stats.sevenDayPct;
        strlcpy(rows[n].resetText, stats.sevenDayReset, sizeof(rows[n].resetText));
        n++;
    }
    // Opus/Sonnet omitted — API rarely returns useful data for these
    return n;
}

void dashboard_init() {
    first_draw = true;
    prev_num_rows = 0;
    memset(prev_rows, 0, sizeof(prev_rows));
}

void dashboard_draw_waiting(const char *message) {
    memset(framebuffer, 0xFF, SCREEN_W * SCREEN_H / 2);

    draw_header();

    int32_t cx = SCREEN_W / 2 - 200;
    int32_t cy = SCREEN_H / 2;
    writeln((GFXfont *)&FiraSans, message, &cx, &cy, framebuffer);

    epd_poweron();
    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();

    first_draw = true;
}

void dashboard_draw(const ClaudeStats &stats) {
    RowData rows[MAX_ROWS];
    int num_rows = build_rows(stats, rows);

    // Compute spacing and vertical centering
    // Each row is ~80px tall (bar + sub-line), center in available space
    row_spacing = 130;
    int total_height = (num_rows > 1) ? (num_rows - 1) * row_spacing + 80 : 80;
    int available_top = DIVIDER_Y + 10;
    int available_bottom = SCREEN_H - FOOTER_H;
    content_top = available_top + (available_bottom - available_top - total_height) / 2;

    if (first_draw || num_rows != prev_num_rows) {
        // Full content redraw (row count changed or first draw)
        memset(framebuffer, 0xFF, SCREEN_W * SCREEN_H / 2);

        draw_header();
        for (int i = 0; i < num_rows; i++) {
            draw_usage_row(i, rows[i]);
        }
        draw_footer(stats.plan);

        epd_poweron();
        epd_clear();
        epd_draw_grayscale_image(epd_full_screen(), framebuffer);
        epd_poweroff();

        prev_num_rows = num_rows;
        memcpy(prev_rows, rows, sizeof(rows));
        first_draw = false;
        return;
    }

    // Partial refresh: only changed rows + footer
    epd_poweron();

    for (int i = 0; i < num_rows; i++) {
        bool changed = (rows[i].pct != prev_rows[i].pct) ||
                       strcmp(rows[i].resetText, prev_rows[i].resetText) != 0;
        if (changed) {
            Rect_t r = row_region(i);
            clear_fb_region(r);
            draw_usage_row(i, rows[i]);
            push_region(r);
        }
    }

    // Footer always updates (time changes)
    Rect_t r = footer_region();
    clear_fb_region(r);
    draw_footer(stats.plan);
    push_region(r);

    epd_poweroff();

    prev_num_rows = num_rows;
    memcpy(prev_rows, rows, sizeof(rows));
}
