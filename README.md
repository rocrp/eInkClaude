# eInkClaude

Claude Pro/Max usage dashboard on a LilyGo T5 4.7" e-paper display.
Shows 5-hour and 7-day utilization with reset countdowns.
Standalone — ESP32-S3 fetches data directly over WiFi.

## Hardware

- LilyGo T5 4.7" e-Paper S3 (EPD47)

## Setup

1. Clone and build:
   ```
   git clone <repo>
   cd eInkClaude
   pio run -t upload
   ```
2. On first boot, the touchscreen setup wizard appears:
   - Select your WiFi network from the scan list
   - Enter WiFi password using on-screen keyboard
   - Enter your Claude OAuth token (see below)
3. Credentials are saved to flash — survives reboots.
4. Hold the side button during boot to re-enter setup.

## Getting Your OAuth Token

1. Install Claude Code: `npm i -g @anthropic-ai/claude-code`
2. Run `claude` and complete the login flow
3. Your token is saved at `~/.claude/.credentials.json`
4. Copy the `accessToken` value (starts with `sk-ant-oat01-`)

## Display

- **5 HOUR** — Rolling 5-hour usage with reset countdown
- **7 DAY** — Rolling 7-day usage with reset countdown
- Updates every 5 minutes with partial e-ink refresh
