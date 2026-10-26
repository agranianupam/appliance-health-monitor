"""
Synthetic Sensor Data Generator for Testing and Pipeline Validation.
Generates realistic multi-session 100 Hz CSV data for 5V DC fan testing:
  - Class 0: Normal operation (firmly mounted, smooth rotation)
  - Class 1: Warning (mild imbalance, slight wobble)
  - Class 2: Critical (heavy vibration, bearing chatter or stall)
"""

import os
import numpy as np
import pandas as pd
from pathlib import Path

FS = 100.0
WINDOW_SIZE = 128
SAMPLES_PER_SEC = 100
GRAVITY = 9.80665

def generate_session(session_name, label, duration_sec=40, seed=42):
    np.random.seed(seed)
    n_samples = int(duration_sec * SAMPLES_PER_SEC)
    t = np.arange(n_samples) / FS

    fan_freq = 30.0 + np.random.uniform(-1.5, 1.5)

    if label == 0:

        vib_amp_x = 0.25 + np.random.uniform(-0.05, 0.05)
        vib_amp_y = 0.20 + np.random.uniform(-0.05, 0.05)
        vib_amp_z = 0.15 + np.random.uniform(-0.03, 0.03)
        noise_level = 0.12
        base_temp = 28.0 + np.random.uniform(-1.0, 1.0)
        temp_drift = 0.005
        base_current = 0.16 + np.random.uniform(-0.01, 0.01)

    elif label == 1:

        vib_amp_x = 0.85 + np.random.uniform(-0.1, 0.1)
        vib_amp_y = 0.70 + np.random.uniform(-0.1, 0.1)
        vib_amp_z = 0.40 + np.random.uniform(-0.05, 0.05)
        noise_level = 0.25
        base_temp = 32.0 + np.random.uniform(-1.0, 1.0)
        temp_drift = 0.04
        base_current = 0.20 + np.random.uniform(-0.02, 0.02)

    else:

        vib_amp_x = 2.4 + np.random.uniform(-0.3, 0.3)
        vib_amp_y = 2.0 + np.random.uniform(-0.2, 0.2)
        vib_amp_z = 1.2 + np.random.uniform(-0.2, 0.2)
        noise_level = 0.6
        base_temp = 38.0 + np.random.uniform(-1.0, 2.0)
        temp_drift = 0.12
        base_current = 0.32 + np.random.uniform(-0.03, 0.03)

    ax = (vib_amp_x * np.sin(2 * np.pi * fan_freq * t) +
          (vib_amp_x * 0.3) * np.sin(2 * np.pi * 2 * fan_freq * t) +
          np.random.normal(0, noise_level, n_samples))

    ay = (vib_amp_y * np.cos(2 * np.pi * fan_freq * t) +
          (vib_amp_y * 0.25) * np.cos(2 * np.pi * 2 * fan_freq * t) +
          np.random.normal(0, noise_level, n_samples))

    az = (GRAVITY +
          vib_amp_z * np.sin(2 * np.pi * fan_freq * t + 0.5) +
          np.random.normal(0, noise_level * 0.8, n_samples))

    temp = base_temp + temp_drift * t + np.random.normal(0, 0.05, n_samples)

    current = base_current + np.random.normal(0, 0.005, n_samples)

    rows = []
    start_ts = int(1700000000000 + seed * 100000)
    for i in range(n_samples):
        win_id = i // WINDOW_SIZE
        s_idx = i % WINDOW_SIZE
        ts = start_ts + int(i * 10)
        rows.append({
            "session_id": session_name,
            "timestamp_ms": ts,
            "window_id": win_id,
            "sample_idx": s_idx,
            "ax": round(float(ax[i]), 4),
            "ay": round(float(ay[i]), 4),
            "az": round(float(az[i]), 4),
            "temp_c": round(float(temp[i]), 2),
            "current_a": round(float(current[i]), 3),
            "label": label
        })

    df = pd.DataFrame(rows)
    return df

def generate_full_dataset(out_dir="data/raw"):
    os.makedirs(out_dir, exist_ok=True)
    sessions = [

        ("sess_normal_day1", 0, 50, 101),
        ("sess_normal_day2", 0, 50, 102),
        ("sess_normal_day3", 0, 40, 103),

        ("sess_warn_tape", 1, 45, 201),
        ("sess_warn_loose", 1, 45, 202),
        ("sess_warn_flow", 1, 40, 203),

        ("sess_crit_imbalance", 2, 45, 301),
        ("sess_crit_stall", 2, 45, 302),
        ("sess_crit_overheat", 2, 40, 303),
    ]

    for name, lbl, dur, s in sessions:
        df = generate_session(name, lbl, duration_sec=dur, seed=s)
        out_path = os.path.join(out_dir, f"{name}.csv")
        df.to_csv(out_path, index=False)
        print(f"[GEN] Generated {out_path} ({len(df)} rows, {len(df)//WINDOW_SIZE} windows)")

if __name__ == "__main__":
    generate_full_dataset()
