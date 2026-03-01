# PC Stats Dashboard — LilyGo T5 4.7" E-Paper

## Overview

A USB-connected PC stats monitor using the LilyGo T5 4.7" E-Paper V2.3 (ESP32-S3).
A Python host script collects system metrics and sends them over USB serial to the ESP32,
which renders a dashboard on the 4.7" e-paper display.

## Architecture

```
┌─────────────┐   USB Serial (JSON)   ┌──────────────────────┐
│  Python PC  │ ───────────────────►   │  T5 4.7" E-Paper     │
│  Host Script│   COM3 @ 115200        │  ESP32-S3-WROOM-1    │
│  (psutil)   │                        │  960x540 e-paper     │
└─────────────┘                        └──────────────────────┘
```

## Hardware

- **Board**: LilyGo T5 4.7" E-Paper V2.3 (2024-12-03 revision)
- **MCU**: ESP32-S3-WROOM-1-N16R8 (16MB flash, 8MB PSRAM)
- **Display**: ED047TC1, 960x540, 16 grayscale levels
- **Connection**: USB-C (native USB CDC on ESP32-S3)
- **USB VID/PID**: 303A:1001

## Data Protocol

JSON lines over USB serial at 115200 baud, sent every 5-10 seconds:

```json
{"cpu": 45, "ram": 62, "temp": 65, "disk": 78, "net_up": 12.5, "net_down": 45.3}
```

Fields:
- `cpu`: CPU usage percentage (0-100)
- `ram`: RAM usage percentage (0-100)
- `temp`: CPU temperature in Celsius
- `disk`: Primary disk usage percentage (0-100)
- `net_up`: Upload speed in Mbps
- `net_down`: Download speed in Mbps

## Display Layout (960x540, landscape)

```
┌─────────────────────────────────────────┐
│            PC STATS MONITOR             │
│─────────────────────────────────────────│
│                                         │
│  CPU ████████████░░░░░░░░  45%          │
│                                         │
│  RAM ██████████████░░░░░░  62%          │
│                                         │
│  TMP ████████████░░░░░░░░  65°C         │
│                                         │
│  DSK ██████████████████░░  78%          │
│                                         │
│  NET  ↑ 12 Mbps   ↓ 45 Mbps            │
│                                         │
│          Last updated: 12:34:05         │
└─────────────────────────────────────────┘
```

- Progress bars use grayscale fills
- Partial refresh to avoid full-screen flicker (only update changed values)
- Color coding via grayscale intensity: light gray = OK, dark = warning, black = critical

## Tech Stack

| Layer          | Choice                          |
|----------------|---------------------------------|
| Build system   | PlatformIO (Arduino framework)  |
| Display lib    | GxEPD2                          |
| JSON parsing   | ArduinoJson                     |
| Host language  | Python 3                        |
| Host libs      | psutil, pyserial                |
| Serial baud    | 115200                          |
| Refresh rate   | 5-10 seconds (partial refresh)  |
| Board platform | espressif32 (esp32-s3)          |

## ESP32 Firmware Components

1. **Serial receiver**: Read JSON lines from USB CDC serial
2. **JSON parser**: Deserialize into stat values using ArduinoJson
3. **Display renderer**: Draw dashboard layout with progress bars and text
4. **Partial refresh manager**: Track dirty regions, only refresh changed areas

## Python Host Script Components

1. **System stats collector**: psutil for CPU, RAM, disk, temps
2. **Network monitor**: psutil net_io_counters for bandwidth calculation
3. **Serial sender**: pyserial to write JSON to COM port
4. **Main loop**: Collect and send every 5 seconds
