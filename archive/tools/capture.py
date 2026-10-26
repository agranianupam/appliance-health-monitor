"""
Python Data Capture Tool for ESP32 TinyML Predictive Maintenance.
Reads streaming CSV rows from ESP32 serial port, logs to data/raw/<session>.csv,
provides live row and window counts per class, and allows setting labels in real time.
"""

import sys
import os
import time
import argparse
from pathlib import Path
from datetime import datetime

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("[ERROR] pyserial is required. Install via: pip install pyserial")
    sys.exit(1)

try:
    import msvcrt
    def get_key_nonblocking():
        if msvcrt.kbhit():
            ch = msvcrt.getch()
            try:
                return ch.decode('utf-8', errors='ignore')
            except Exception:
                return None
        return None
except ImportError:
    import select
    def get_key_nonblocking():
        dr, _, _ = select.select([sys.stdin], [], [], 0)
        if dr:
            return sys.stdin.read(1)
        return None

CSV_HEADER = "session_id,timestamp_ms,window_id,sample_idx,ax,ay,az,temp_c,current_a,label\n"

CLASS_NAMES = {
    0: "Normal",
    1: "Warning",
    2: "Critical"
}

def auto_detect_esp32_port():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        desc = (p.description or "").lower()
        hwid = (p.hwid or "").lower()
        if any(keyword in desc or keyword in hwid for keyword in ["cp210", "ch340", "ftdi", "usb serial", "esp32"]):
            return p.device
    if ports:
        return ports[0].device
    return None

def main():
    parser = argparse.ArgumentParser(description="Capture raw sensor telemetry from ESP32")
    parser.add_argument("--port", "-p", default=None, help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baud", "-b", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--session", "-s", default=None, help="Session name (default: session_YYYYMMDD_HHMMSS)")
    parser.add_argument("--outdir", "-o", default="data/raw", help="Output directory for raw CSVs")
    args = parser.parse_args()

    port = args.port or auto_detect_esp32_port()
    if not port:
        print("[ERROR] No serial port detected. Please specify using --port (e.g. --port COM3)")
        sys.exit(1)

    session_name = args.session or f"session_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    out_dir = Path(args.outdir)
    out_dir.mkdir(parents=True, exist_ok=True)
    out_file = out_dir / f"{session_name}.csv"

    print(f"==================================================")
    print(f" ESP32 TinyML Data Capture Tool")
    print(f" Port:    {port} @ {args.baud} baud")
    print(f" Output:  {out_file}")
    print(f" Session: {session_name}")
    print(f" Controls:")
    print(f"   [0] Mark as Class 0 (NORMAL)")
    print(f"   [1] Mark as Class 1 (WARNING)")
    print(f"   [2] Mark as Class 2 (CRITICAL)")
    print(f"   [s] Pause / Resume ESP32 logging")
    print(f"   [q] Save and quit")
    print(f"==================================================")

    try:
        ser = serial.Serial(port, args.baud, timeout=0.1)
    except Exception as e:
        print(f"[ERROR] Could not open port {port}: {e}")
        sys.exit(1)

    time.sleep(1.5)
    ser.reset_input_buffer()

    ser.write(f"i:{session_name}\n".encode('utf-8'))
    time.sleep(0.1)

    counts = {0: 0, 1: 0, 2: 0}
    windows_seen = set()
    current_label = 0
    is_paused = False

    file_exists = out_file.exists()
    f = open(out_file, "a", buffering=1, encoding="utf-8")
    if not file_exists or out_file.stat().st_size == 0:
        f.write(CSV_HEADER)

    last_ui_update = 0

    try:
        while True:

            key = get_key_nonblocking()
            if key:
                if key in ['0', '1', '2']:
                    current_label = int(key)
                    ser.write(key.encode('utf-8'))
                    print(f"\n[KEYBOARD] Set active label to {current_label} ({CLASS_NAMES[current_label]})")
                elif key.lower() == 's':
                    is_paused = not is_paused
                    ser.write(b's')
                    print(f"\n[KEYBOARD] Toggled pause -> {'PAUSED' if is_paused else 'RESUMED'}")
                elif key.lower() == 'q':
                    print("\n[KEYBOARD] Quitting capture...")
                    break

            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if not line:
                continue

            if line.startswith('#'):
                print(f"[ESP32] {line}")
                continue
            if line.startswith("session_id"):
                continue

            parts = line.split(',')
            if len(parts) >= 10:
                try:
                    win_id = int(parts[2])
                    lbl = int(parts[9])
                    if lbl in counts:
                        counts[lbl] += 1
                    windows_seen.add(win_id)
                    f.write(line + "\n")
                except ValueError:
                    continue

            now = time.time()
            if now - last_ui_update > 0.25:
                last_ui_update = now
                total_samples = sum(counts.values())
                win_c0 = counts[0] // 128
                win_c1 = counts[1] // 128
                win_c2 = counts[2] // 128
                state_str = "PAUSED" if is_paused else "LOGGING"
                status = (
                    f"\r[{state_str}] Active: {current_label} ({CLASS_NAMES[current_label]:<8}) | "
                    f"Norm: {counts[0]:>5} ({win_c0}w) | "
                    f"Warn: {counts[1]:>5} ({win_c1}w) | "
                    f"Crit: {counts[2]:>5} ({win_c2}w) | "
                    f"Total: {total_samples:>5} ({len(windows_seen)}w)"
                )
                sys.stdout.write(status)
                sys.stdout.flush()

    except KeyboardInterrupt:
        print("\n[INFO] Capture stopped by user (Ctrl+C).")
    finally:
        f.close()
        ser.close()
        print(f"\n[DONE] Saved session to: {out_file.resolve()}")
        print(f"Summary:")
        print(f"  - Class 0 (Normal):   {counts[0]} samples ({counts[0] // 128} full windows)")
        print(f"  - Class 1 (Warning):  {counts[1]} samples ({counts[1] // 128} full windows)")
        print(f"  - Class 2 (Critical): {counts[2]} samples ({counts[2] // 128} full windows)")
        print(f"  - Total Samples:      {sum(counts.values())}")

if __name__ == "__main__":
    main()
