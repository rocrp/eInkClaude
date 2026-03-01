# PC Stats Dashboard Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a USB-connected PC stats monitor that displays CPU, RAM, temp, disk, and network stats on a LilyGo T5 4.7" e-paper display.

**Architecture:** A Python host script collects system metrics via psutil and sends JSON over USB serial (COM3) to an ESP32-S3, which parses the data and renders a dashboard on the 960x540 e-paper display using the LilyGo epd_driver library. The display uses partial refresh every 5-10 seconds to avoid full-screen flicker.

**Tech Stack:** PlatformIO (Arduino framework), espressif32@6.5.0, LilyGo-EPD47 epd_driver library, ArduinoJson, Python 3, psutil, pyserial

---

## Task 1: Initialize PlatformIO Project

**Files:**
- Create: `platformio.ini`
- Create: `boards/T5-ePaper-S3.json`
- Create: `src/main.cpp`

**Step 1: Initialize PlatformIO project**

Run: `cd C:/DevOps/LilyGoScreenClaud && ~/.platformio/penv/Scripts/pio.exe init --board esp32-s3-devkitc-1`

**Step 2: Create the custom board definition**

Create `boards/T5-ePaper-S3.json`:
```json
{
  "build": {
    "arduino": {
      "ldscript": "esp32s3_out.ld",
      "memory_type": "qio_opi",
      "partitions": "default_16MB.csv"
    },
    "core": "esp32",
    "extra_flags": [
      "-DBOARD_HAS_PSRAM",
      "-DARDUINO_USB_MODE=1"
    ],
    "f_cpu": "240000000L",
    "f_flash": "80000000L",
    "flash_mode": "qio",
    "hwids": [
      ["0x303A", "0x1001"]
    ],
    "mcu": "esp32s3",
    "variant": "esp32s3"
  },
  "connectivity": ["wifi", "bluetooth"],
  "debug": {
    "openocd_target": "esp32s3.cfg"
  },
  "frameworks": ["arduino"],
  "name": "LilyGo T5-ePaper-S3",
  "upload": {
    "flash_size": "16MB",
    "maximum_ram_size": 327680,
    "maximum_size": 16777216,
    "require_upload_port": true,
    "speed": 921600
  },
  "url": "https://www.lilygo.cc/products/t5-4-7-inch-e-paper-v2-3",
  "vendor": "LILYGO"
}
```

**Step 3: Write platformio.ini**

Replace contents of `platformio.ini`:
```ini
[platformio]
boards_dir = boards

[env:T5-ePaper-S3]
platform = espressif32@6.5.0
board = T5-ePaper-S3
framework = arduino
upload_speed = 921600
monitor_speed = 115200
monitor_filters = default, esp32_exception_decoder

build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DLILYGO_T5_EPD47_S3
    -DCORE_DEBUG_LEVEL=0

lib_deps =
    https://github.com/Xinyuan-LilyGO/LilyGo-EPD47.git
    bblanchon/ArduinoJson@^7.4.2
```

**Step 4: Create minimal main.cpp to verify build**

Create `src/main.cpp`:
```cpp
#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    Serial.println("PC Stats Dashboard booting...");
}

void loop() {
    delay(1000);
}
```

**Step 5: Build to verify setup**

Run: `cd C:/DevOps/LilyGoScreenClaud && ~/.platformio/penv/Scripts/pio.exe run`
Expected: BUILD SUCCESS

**Step 6: Commit**

```bash
git init
git add platformio.ini boards/ src/main.cpp
git commit -m "feat: initialize PlatformIO project for T5 4.7 e-paper"
```

---

## Task 2: Display Hello World on E-Paper

**Files:**
- Modify: `src/main.cpp`

**Step 1: Write hello world firmware**

Replace `src/main.cpp`:
```cpp
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
```

**Step 2: Build and upload**

Run: `~/.platformio/penv/Scripts/pio.exe run -t upload`
Expected: SUCCESS, display shows "Hello from PC Stats Dashboard!"

**Step 3: Verify on serial monitor**

Run: `~/.platformio/penv/Scripts/pio.exe device monitor`
Expected: "EPD Hello World" printed on serial

**Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: hello world on e-paper display"
```

---

## Task 3: Build Dashboard Renderer

**Files:**
- Modify: `src/main.cpp`
- Create: `src/dashboard.h`
- Create: `src/dashboard.cpp`

**Step 1: Create dashboard header**

Create `src/dashboard.h`:
```cpp
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
```

**Step 2: Create dashboard implementation**

Create `src/dashboard.cpp`:
```cpp
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

    // Draw to screen using partial refresh (area-based clear + redraw)
    epd_poweron();
    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}
```

**Step 3: Update main.cpp to use dashboard with serial JSON parsing**

Replace `src/main.cpp`:
```cpp
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
```

**Step 4: Build**

Run: `~/.platformio/penv/Scripts/pio.exe run`
Expected: BUILD SUCCESS

**Step 5: Upload and test with serial**

Run: `~/.platformio/penv/Scripts/pio.exe run -t upload`
Then send test JSON from serial monitor:
```
{"cpu":45,"ram":62,"temp":65,"disk":78,"net_up":12.5,"net_down":45.3}
```
Expected: Dashboard renders with the test values

**Step 6: Commit**

```bash
git add src/
git commit -m "feat: add dashboard renderer with serial JSON parsing"
```

---

## Task 4: Create Python Host Script

**Files:**
- Create: `host/pc_stats.py`
- Create: `host/requirements.txt`

**Step 1: Create requirements.txt**

Create `host/requirements.txt`:
```
psutil>=5.9.0
pyserial>=3.5
```

**Step 2: Create the host script**

Create `host/pc_stats.py`:
```python
"""PC Stats Monitor - sends system stats to ESP32 over serial."""

import json
import time
import sys
import argparse

import psutil
import serial
import serial.tools.list_ports


def find_esp32_port():
    """Auto-detect ESP32-S3 on USB (VID 0x303A)."""
    for port in serial.tools.list_ports.comports():
        if port.vid == 0x303A:
            return port.device
    return None


def get_stats():
    """Collect system stats."""
    cpu = psutil.cpu_percent(interval=None)
    ram = psutil.virtual_memory().percent
    disk = psutil.disk_usage("/").percent

    # CPU temperature (Windows: requires OpenHardwareMonitor/LibreHardwareMonitor)
    temp = 0
    try:
        temps = psutil.sensors_temperatures()
        if temps:
            for name, entries in temps.items():
                if entries:
                    temp = int(entries[0].current)
                    break
    except (AttributeError, KeyError):
        pass

    # Network throughput
    return {
        "cpu": int(cpu),
        "ram": int(ram),
        "temp": temp,
        "disk": int(disk),
    }


def main():
    parser = argparse.ArgumentParser(description="PC Stats Monitor")
    parser.add_argument("--port", default=None, help="Serial port (auto-detect if omitted)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--interval", type=float, default=5.0, help="Update interval in seconds")
    args = parser.parse_args()

    port = args.port or find_esp32_port()
    if not port:
        print("ERROR: ESP32 not found. Specify --port manually.")
        sys.exit(1)

    print(f"Connecting to {port} at {args.baud} baud...")
    ser = serial.Serial(port, args.baud, timeout=1)
    time.sleep(2)  # Wait for ESP32 to boot

    print(f"Connected. Sending stats every {args.interval}s. Press Ctrl+C to stop.")

    # Initialize network counters for throughput calculation
    prev_net = psutil.net_io_counters()
    prev_time = time.time()

    # Prime the CPU percent measurement
    psutil.cpu_percent(interval=None)

    try:
        while True:
            time.sleep(args.interval)

            stats = get_stats()

            # Calculate network throughput
            now = time.time()
            cur_net = psutil.net_io_counters()
            elapsed = now - prev_time
            if elapsed > 0:
                stats["net_up"] = round((cur_net.bytes_sent - prev_net.bytes_sent) * 8 / elapsed / 1_000_000, 1)
                stats["net_down"] = round((cur_net.bytes_recv - prev_net.bytes_recv) * 8 / elapsed / 1_000_000, 1)
            else:
                stats["net_up"] = 0.0
                stats["net_down"] = 0.0
            prev_net = cur_net
            prev_time = now

            line = json.dumps(stats) + "\n"
            ser.write(line.encode())

            # Read response
            if ser.in_waiting:
                resp = ser.readline().decode(errors="ignore").strip()
                if resp:
                    print(f"[{time.strftime('%H:%M:%S')}] Sent: {stats} | ESP32: {resp}")
            else:
                print(f"[{time.strftime('%H:%M:%S')}] Sent: {stats}")

    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
```

**Step 3: Install Python dependencies**

Run: `pip install psutil pyserial`

**Step 4: Test the host script (with ESP32 connected and flashed)**

Run: `python host/pc_stats.py`
Expected: Detects COM3, starts sending stats, ESP32 display updates

**Step 5: Commit**

```bash
git add host/
git commit -m "feat: add Python host script for PC stats collection"
```

---

## Task 5: End-to-End Test and Polish

**Step 1: Flash firmware**

Run: `~/.platformio/penv/Scripts/pio.exe run -t upload`

**Step 2: Run host script**

Run: `python host/pc_stats.py`

**Step 3: Verify display**

Confirm:
- Display shows "Waiting for PC connection..." initially
- After host script connects, dashboard renders with live stats
- Stats update every ~5 seconds with partial refresh
- Values look correct (compare with Task Manager)

**Step 4: Final commit**

```bash
git add -A
git commit -m "feat: PC stats dashboard v1.0 complete"
```
