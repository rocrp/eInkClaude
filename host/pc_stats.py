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
