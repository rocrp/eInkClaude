#include "touch_ui.h"
#include <Wire.h>
#include <WiFi.h>
#include <TouchDrvGT911.hpp>
#include "epd_driver.h"
#include "utilities.h"
#include "firasans.h"
#include "firasans_small.h"

extern uint8_t *framebuffer;

static TouchDrvGT911 touch;
static const int SW = 960;
static const int SH = 540;

// ── Background touch task (forward declarations) ────────────────────────────
static void touch_task(void *param);
static volatile bool tap_ready = false;
static volatile int16_t tap_buf_x = 0;
static volatile int16_t tap_buf_y = 0;
static volatile bool drain_requested = false;
static volatile bool touch_task_running = false;
static portMUX_TYPE tap_mux = portMUX_INITIALIZER_UNLOCKED;

// Screen label for debug logging
static char screen_label[16] = "boot";
static portMUX_TYPE label_mux = portMUX_INITIALIZER_UNLOCKED;

// ── Touch driver ────────────────────────────────────────────────────────────

bool touch_init() {
    Wire.begin(BOARD_SDA, BOARD_SCL);

    // Wake touch controller
    pinMode(TOUCH_INT, OUTPUT);
    digitalWrite(TOUCH_INT, HIGH);
    delay(50);

    // Scan for GT911 address
    uint8_t addr = 0;
    Wire.beginTransmission(0x14);
    if (Wire.endTransmission() == 0) addr = 0x14;
    Wire.beginTransmission(0x5D);
    if (Wire.endTransmission() == 0) addr = 0x5D;

    if (addr == 0) {
        Serial.println("GT911 not found");
        return false;
    }

    touch.setPins(-1, TOUCH_INT);
    if (!touch.begin(Wire, addr, BOARD_SDA, BOARD_SCL)) {
        Serial.println("GT911 begin failed");
        return false;
    }
    touch.setMaxCoordinates(EPD_WIDTH, EPD_HEIGHT);
    touch.setSwapXY(true);
    touch.setMirrorXY(false, true);

    Serial.printf("GT911 ready at 0x%02X\n", addr);

    // Start background touch polling on core 0 (main loop runs on core 1)
    xTaskCreatePinnedToCore(touch_task, "touch", 4096, NULL, 2, NULL, 0);
    touch_task_running = true;
    Serial.println("Touch polling task started on core 0");

    return true;
}

static void touch_task(void *param) {
    bool was_touching = false;
    int release_frames = 0;
    const int RELEASE_THRESHOLD = 5;
    uint32_t poll_count = 0;
    uint32_t touch_count = 0;
    uint32_t last_stats_ms = 0;

    while (true) {
        // Handle drain requests from main thread
        if (drain_requested) {
            Serial.printf("[TOUCH] drain start (screen=%s)\n", screen_label);
            was_touching = true;
            vTaskDelay(pdMS_TO_TICKS(300));
            int16_t dx, dy;
            int drained = 0;
            for (int i = 0; i < 20; i++) {
                if (!touch.getPoint(&dx, &dy)) break;
                drained++;
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            was_touching = false;
            // Clear any pending tap
            portENTER_CRITICAL(&tap_mux);
            tap_ready = false;
            drain_requested = false;
            portEXIT_CRITICAL(&tap_mux);
            Serial.printf("[TOUCH] drain done, discarded %d points\n", drained);
            continue;
        }

        int16_t tx, ty;
        uint8_t touched = touch.getPoint(&tx, &ty);
        poll_count++;

        if (touched) {
            touch_count++;
            release_frames = 0;

            // Get screen label safely
            char lbl[16];
            portENTER_CRITICAL(&label_mux);
            memcpy(lbl, screen_label, sizeof(lbl));
            portEXIT_CRITICAL(&label_mux);

            if (!was_touching) {
                was_touching = true;
                // Store tap — main thread will pick it up
                bool stored = false;
                portENTER_CRITICAL(&tap_mux);
                if (!tap_ready) {  // Don't overwrite unread tap
                    tap_buf_x = tx;
                    tap_buf_y = ty;
                    tap_ready = true;
                    stored = true;
                }
                portEXIT_CRITICAL(&tap_mux);
                Serial.printf("[TOUCH] NEW tap (%d,%d) screen=%s stored=%d\n",
                              tx, ty, lbl, stored);
            } else {
                Serial.printf("[TOUCH] held (%d,%d) screen=%s\n", tx, ty, lbl);
            }
        } else {
            if (was_touching && release_frames < RELEASE_THRESHOLD) {
                release_frames++;
                if (release_frames >= RELEASE_THRESHOLD) {
                    was_touching = false;
                    Serial.printf("[TOUCH] released (screen=%s)\n", screen_label);
                }
            } else {
                release_frames++;
                if (release_frames >= RELEASE_THRESHOLD) {
                    was_touching = false;
                }
            }
        }

        // Print stats every 5 seconds
        uint32_t now_ms = (uint32_t)millis();
        if (now_ms - last_stats_ms >= 5000) {
            Serial.printf("[TOUCH] stats: %u polls, %u touches in 5s (screen=%s)\n",
                          poll_count, touch_count, screen_label);
            poll_count = 0;
            touch_count = 0;
            last_stats_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(15));  // ~66Hz polling, independent of main loop
    }
}

bool touch_get_tap(int16_t &x, int16_t &y) {
    portENTER_CRITICAL(&tap_mux);
    if (tap_ready) {
        x = tap_buf_x;
        y = tap_buf_y;
        tap_ready = false;
        portEXIT_CRITICAL(&tap_mux);
        Serial.printf("[TOUCH] get_tap -> (%d,%d) screen=%s\n", x, y, screen_label);
        return true;
    }
    portEXIT_CRITICAL(&tap_mux);
    return false;
}

void touch_drain() {
    if (!touch_task_running) return;  // No task to coordinate with
    portENTER_CRITICAL(&tap_mux);
    drain_requested = true;
    tap_ready = false;
    portEXIT_CRITICAL(&tap_mux);
    // Wait for background task to complete the drain
    while (drain_requested) {
        delay(10);
    }
}

void touch_set_screen(const char *label) {
    portENTER_CRITICAL(&label_mux);
    strncpy(screen_label, label, sizeof(screen_label) - 1);
    screen_label[sizeof(screen_label) - 1] = '\0';
    portEXIT_CRITICAL(&label_mux);
    Serial.printf("[TOUCH] screen set to '%s'\n", label);
}

// ── Drawing helpers ─────────────────────────────────────────────────────────

static void fb_clear() {
    memset(framebuffer, 0xFF, SW * SH / 2);
}

static void fb_push_full() {
    epd_poweron();
    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}

static void clear_fb_region(const Rect_t &r) {
    for (int y = r.y; y < r.y + r.height && y < SH; y++) {
        memset(&framebuffer[y * SW / 2 + r.x / 2], 0xFF, r.width / 2);
    }
}

static void push_region(const Rect_t &r) {
    uint8_t *data_ptr = framebuffer + r.y * (SW / 2);
    epd_poweron();
    epd_clear_area(r);
    epd_draw_grayscale_image(r, data_ptr);
    epd_poweroff();
}

static void draw_text(int x, int y, const char *text, const GFXfont *font) {
    int32_t cx = x, cy = y;
    writeln((GFXfont *)font, text, &cx, &cy, framebuffer);
}

static void draw_text_centered(int y, const char *text, const GFXfont *font) {
    int32_t x1, y1, tw, th;
    int32_t tx = 0, ty = 0;
    get_text_bounds((GFXfont *)font, text, &tx, &ty, &x1, &y1, &tw, &th, NULL);
    draw_text((SW - tw) / 2, y, text, font);
}

static void draw_filled_button(int x, int y, int w, int h, const char *label, bool inverted) {
    if (inverted) {
        epd_fill_rect(x, y, w, h, 0x00, framebuffer);
        epd_fill_rect(x + 2, y + 2, w - 4, h - 4, 0x30, framebuffer);
    } else {
        epd_draw_rect(x, y, w, h, 0x00, framebuffer);
    }
    int32_t x1, y1, tw, th;
    int32_t tx = 0, ty = 0;
    get_text_bounds((GFXfont *)&FiraSansSmall, label, &tx, &ty, &x1, &y1, &tw, &th, NULL);
    draw_text(x + (w - tw) / 2, y + h / 2 + th / 2, label, &FiraSansSmall);
}

static bool in_rect(int16_t px, int16_t py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

// ── Signal strength bars ────────────────────────────────────────────────────

static void draw_signal_bars(int x, int y, int rssi) {
    int bars = 0;
    if (rssi > -50) bars = 4;
    else if (rssi > -60) bars = 3;
    else if (rssi > -70) bars = 2;
    else if (rssi > -85) bars = 1;

    for (int i = 0; i < 4; i++) {
        int bx = x + i * 8;
        int bh = 6 + i * 5;
        int by = y + 26 - bh;
        if (i < bars) {
            epd_fill_rect(bx, by, 5, bh, 0x00, framebuffer);
        } else {
            epd_fill_rect(bx, by, 5, bh, 0xC0, framebuffer);
        }
    }
}

// ── WiFi Selector ───────────────────────────────────────────────────────────

static const int WIFI_ROWS_PER_PAGE = 5;
static const int WIFI_ROW_H = 60;
static const int WIFI_LIST_TOP = 80;
static const int WIFI_LIST_X = 40;
static const int WIFI_LIST_W = 880;

struct WifiEntry {
    char ssid[33];
    int32_t rssi;
};

static void draw_wifi_screen(WifiEntry *entries, int count, int page, int selected) {
    fb_clear();

    draw_text_centered(50, "Select WiFi Network", &FiraSans);
    epd_fill_rect(40, 62, SW - 80, 2, 0x00, framebuffer);

    int start = page * WIFI_ROWS_PER_PAGE;
    int end = start + WIFI_ROWS_PER_PAGE;
    if (end > count) end = count;

    for (int i = start; i < end; i++) {
        int row = i - start;
        int y = WIFI_LIST_TOP + row * WIFI_ROW_H;

        if (i == selected) {
            epd_fill_rect(WIFI_LIST_X, y, WIFI_LIST_W, WIFI_ROW_H - 4, 0xE0, framebuffer);
            draw_text(WIFI_LIST_X + 10, y + 36, ">", &FiraSans);
        }

        draw_text(WIFI_LIST_X + 40, y + 36, entries[i].ssid, &FiraSansSmall);

        draw_signal_bars(WIFI_LIST_X + WIFI_LIST_W - 90, y + 10, entries[i].rssi);

        char rssi_str[8];
        snprintf(rssi_str, sizeof(rssi_str), "%d", entries[i].rssi);
        draw_text(WIFI_LIST_X + WIFI_LIST_W - 45, y + 36, rssi_str, &FiraSansSmall);
    }

    int btn_y = SH - 65;
    int total_pages = (count + WIFI_ROWS_PER_PAGE - 1) / WIFI_ROWS_PER_PAGE;

    // Cancel button (bottom-left)
    draw_filled_button(40, btn_y, 80, 50, "X", false);

    if (page > 0)
        draw_filled_button(140, btn_y, 120, 50, "UP", false);
    if (page < total_pages - 1)
        draw_filled_button(280, btn_y, 120, 50, "DOWN", false);
    draw_filled_button(SW - 280, btn_y, 120, 50, "RESCAN", false);
    draw_filled_button(SW - 140, btn_y, 120, 50, "SELECT", false);

    char pg[16];
    snprintf(pg, sizeof(pg), "%d / %d", page + 1, total_pages > 0 ? total_pages : 1);
    draw_text(SW / 2 - 20, btn_y + 32, pg, &FiraSansSmall);

    fb_push_full();
    touch_drain();
}

bool run_wifi_selector(char *ssid_out) {
    touch_set_screen("wifi_sel");

    // Show scanning screen immediately so user knows the tap registered
    fb_clear();
    draw_text_centered(SH / 2, "Scanning WiFi...", &FiraSans);
    fb_push_full();
    touch_drain();

    Serial.println("Scanning WiFi...");
    int n = WiFi.scanNetworks();
    if (n <= 0) {
        Serial.println("No networks found, retrying...");
        delay(1000);
        n = WiFi.scanNetworks();
    }

    int count = n > 20 ? 20 : n;
    WifiEntry entries[20];
    for (int i = 0; i < count; i++) {
        strncpy(entries[i].ssid, WiFi.SSID(i).c_str(), 32);
        entries[i].ssid[32] = '\0';
        entries[i].rssi = WiFi.RSSI(i);
    }
    WiFi.scanDelete();

    if (count == 0) {
        fb_clear();
        draw_text_centered(SH / 2, "No WiFi networks found!", &FiraSans);
        draw_filled_button(SW / 2 - 60, SH / 2 + 40, 120, 50, "RESCAN", false);
        fb_push_full();
        touch_drain();

        while (true) {
            int16_t tx, ty;
            if (touch_get_tap(tx, ty)) {
                return run_wifi_selector(ssid_out);
            }
            delay(50);
        }
    }

    int page = 0;
    int selected = 0;
    draw_wifi_screen(entries, count, page, selected);

    while (true) {
        int16_t tx, ty;
        if (!touch_get_tap(tx, ty)) {
            delay(50);
            continue;
        }

        int btn_y = SH - 65;

        // Cancel (X) button
        if (in_rect(tx, ty, 40, btn_y, 80, 50)) {
            return false;
        }

        if (page > 0 && in_rect(tx, ty, 140, btn_y, 120, 50)) {
            page--;
            selected = page * WIFI_ROWS_PER_PAGE;
            draw_wifi_screen(entries, count, page, selected);
            continue;
        }

        int total_pages = (count + WIFI_ROWS_PER_PAGE - 1) / WIFI_ROWS_PER_PAGE;
        if (page < total_pages - 1 && in_rect(tx, ty, 280, btn_y, 120, 50)) {
            page++;
            selected = page * WIFI_ROWS_PER_PAGE;
            draw_wifi_screen(entries, count, page, selected);
            continue;
        }

        if (in_rect(tx, ty, SW - 280, btn_y, 120, 50)) {
            fb_clear();
            draw_text_centered(SH / 2, "Scanning...", &FiraSans);
            fb_push_full();
            return run_wifi_selector(ssid_out);
        }

        if (in_rect(tx, ty, SW - 140, btn_y, 120, 50)) {
            strncpy(ssid_out, entries[selected].ssid, 64);
            ssid_out[63] = '\0';
            return true;
        }

        // Tap on a WiFi row
        int start = page * WIFI_ROWS_PER_PAGE;
        for (int i = 0; i < WIFI_ROWS_PER_PAGE && (start + i) < count; i++) {
            int ry = WIFI_LIST_TOP + i * WIFI_ROW_H;
            if (in_rect(tx, ty, WIFI_LIST_X, ry, WIFI_LIST_W, WIFI_ROW_H)) {
                selected = start + i;
                draw_wifi_screen(entries, count, page, selected);
                break;
            }
        }
    }
}

// ── On-Screen Keyboard ─────────────────────────────────────────────────────

static const char *kb_rows_lower[] = {
    "1234567890-_",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm.@"
};
static const char *kb_rows_upper[] = {
    "1234567890-_",
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM.@"
};
static const int kb_row_count = 4;

static const int KEY_W = 65;
static const int KEY_H = 55;
static const int KEY_GAP = 5;
static const int KB_TOP = 250;
static const int INPUT_Y = 60;
static const int INPUT_H = 80;

static const int SHIFT_X = 30;
static const int SHIFT_W = 75;
static const int SPECIAL_ROW_Y = KB_TOP + 4 * (KEY_H + KEY_GAP);
static const int SPACE_X = 80;
static const int SPACE_W = 500;
static const int DEL_X = 620;
static const int DEL_W = 120;
static const int OK_X = 780;
static const int OK_W = 140;

static int kb_row_x(int row, int num_keys) {
    int total_w = num_keys * KEY_W + (num_keys - 1) * KEY_GAP;
    return (SW - total_w) / 2;
}

// Input field region for partial refresh
static Rect_t input_region() {
    return {0, INPUT_Y, SW, INPUT_H + 20};
}

// Draw just the input field area into framebuffer
static void draw_input_field(const char *input, bool is_password) {
    Rect_t r = input_region();
    clear_fb_region(r);

    // Input field border
    epd_draw_rect(40, INPUT_Y + 20, SW - 80, INPUT_H, 0x00, framebuffer);

    // Build display string
    char display[256];
    int len = strlen(input);
    if (is_password && len > 0) {
        for (int i = 0; i < len - 1 && i < 254; i++) display[i] = '*';
        if (len <= 255) display[len - 1] = input[len - 1];
        display[len] = '\0';
    } else {
        strncpy(display, input, 255);
        display[255] = '\0';
    }
    // Cursor
    int dlen = strlen(display);
    if (dlen < 254) {
        display[dlen] = '_';
        display[dlen + 1] = '\0';
    }
    draw_text(60, INPUT_Y + 70, display, &FiraSans);

    // Character count
    char cntbuf[16];
    snprintf(cntbuf, sizeof(cntbuf), "%d", len);
    draw_text(SW - 100, INPUT_Y + 70, cntbuf, &FiraSansSmall);
}

// Full keyboard screen draw (used on initial draw and shift toggle)
static void draw_keyboard_full(const char *prompt, const char *input,
                                bool is_password, bool shifted) {
    fb_clear();

    // Prompt
    draw_text_centered(40, prompt, &FiraSans);
    epd_fill_rect(40, 52, SW - 80, 2, 0x00, framebuffer);

    // Input field
    draw_input_field(input, is_password);

    // Keyboard rows
    const char **rows = shifted ? kb_rows_upper : kb_rows_lower;
    for (int r = 0; r < kb_row_count; r++) {
        int num_keys = strlen(rows[r]);
        int start_x;
        if (r == 3) {
            start_x = SHIFT_X + SHIFT_W + KEY_GAP;
        } else {
            start_x = kb_row_x(r, num_keys);
        }
        int y = KB_TOP + r * (KEY_H + KEY_GAP);

        for (int k = 0; k < num_keys; k++) {
            int x = start_x + k * (KEY_W + KEY_GAP);
            char lbl[2] = {rows[r][k], '\0'};
            draw_filled_button(x, y, KEY_W, KEY_H, lbl, false);
        }
    }

    // SHIFT
    int shift_y = KB_TOP + 3 * (KEY_H + KEY_GAP);
    draw_filled_button(SHIFT_X, shift_y, SHIFT_W, KEY_H, "SHIFT", shifted);

    // Bottom row
    draw_filled_button(SPACE_X, SPECIAL_ROW_Y, SPACE_W, KEY_H, "SPACE", false);
    draw_filled_button(DEL_X, SPECIAL_ROW_Y, DEL_W, KEY_H, "DEL", false);
    draw_filled_button(OK_X, SPECIAL_ROW_Y, OK_W, KEY_H, "OK", false);

    fb_push_full();
    touch_drain();
}

bool run_keyboard_input(const char *prompt, char *buf, size_t maxlen, bool is_password) {
    buf[0] = '\0';
    int cursor = 0;
    bool shifted = false;

    draw_keyboard_full(prompt, buf, is_password, shifted);

    while (true) {
        int16_t tx, ty;
        if (!touch_get_tap(tx, ty)) {
            delay(50);
            continue;
        }

        bool need_input_refresh = false;
        bool need_full_refresh = false;

        // Check OK
        if (in_rect(tx, ty, OK_X, SPECIAL_ROW_Y, OK_W, KEY_H)) {
            return true;
        }

        // Check DEL
        if (in_rect(tx, ty, DEL_X, SPECIAL_ROW_Y, DEL_W, KEY_H)) {
            if (cursor > 0) {
                cursor--;
                buf[cursor] = '\0';
                need_input_refresh = true;
            }
        }

        // Check SPACE
        else if (in_rect(tx, ty, SPACE_X, SPECIAL_ROW_Y, SPACE_W, KEY_H)) {
            if (cursor < (int)maxlen - 1) {
                buf[cursor++] = ' ';
                buf[cursor] = '\0';
                need_input_refresh = true;
            }
        }

        // Check SHIFT
        else {
            int shift_y = KB_TOP + 3 * (KEY_H + KEY_GAP);
            if (in_rect(tx, ty, SHIFT_X, shift_y, SHIFT_W, KEY_H)) {
                shifted = !shifted;
                need_full_refresh = true;
            } else {
                // Check character keys
                const char **rows = shifted ? kb_rows_upper : kb_rows_lower;
                for (int r = 0; r < kb_row_count; r++) {
                    int num_keys = strlen(rows[r]);
                    int start_x;
                    if (r == 3) {
                        start_x = SHIFT_X + SHIFT_W + KEY_GAP;
                    } else {
                        start_x = kb_row_x(r, num_keys);
                    }
                    int y = KB_TOP + r * (KEY_H + KEY_GAP);

                    bool found = false;
                    for (int k = 0; k < num_keys; k++) {
                        int x = start_x + k * (KEY_W + KEY_GAP);
                        if (in_rect(tx, ty, x, y, KEY_W, KEY_H)) {
                            if (cursor < (int)maxlen - 1) {
                                buf[cursor++] = rows[r][k];
                                buf[cursor] = '\0';
                                need_input_refresh = true;
                            }
                            found = true;
                            break;
                        }
                    }
                    if (found) break;
                }
            }
        }

        if (need_full_refresh) {
            // Shift toggled — redraw entire keyboard
            draw_keyboard_full(prompt, buf, is_password, shifted);
        } else if (need_input_refresh) {
            // Just a character typed/deleted — only refresh input field
            draw_input_field(buf, is_password);
            push_region(input_region());
        }
    }
}
