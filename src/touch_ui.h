#pragma once
#include <Arduino.h>

// Initialize GT911 touch driver. Call once after epd_init().
bool touch_init();

// Returns true if a tap was detected (with debounce). Writes coords to x, y.
bool touch_get_tap(int16_t &x, int16_t &y);

// Interactive WiFi selector. Scans networks, lets user pick one.
// Writes selected SSID into ssid_out (max 64 chars). Returns true on selection.
bool run_wifi_selector(char *ssid_out);

// On-screen keyboard input. Shows prompt, returns entered string in buf.
// is_password: masks characters with *.
// Returns true on OK, false if cancelled (not currently possible — always OK).
bool run_keyboard_input(const char *prompt, char *buf, size_t maxlen, bool is_password);
