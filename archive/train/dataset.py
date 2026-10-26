"""
Dataset Builder for TinyML Training.
Reads raw sensor CSV files from data/raw/, groups into 128-sample windows,
extracts features, preserves session boundaries for GroupKFold validation.
"""

import os
import glob
import pandas as pd
import numpy as np
from pathlib import Path
from features import extract_window_features, FEATURE_NAMES, WINDOW_SIZE

def load_and_process_dataset(raw_dir="data/raw"):
    csv_files = glob.glob(os.path.join(raw_dir, "*.csv"))
    if not csv_files:
        raise FileNotFoundError(f"No CSV files found in {raw_dir}")

    all_features = []
    all_labels = []
    all_sessions = []
    window_ids = []

    for file_path in sorted(csv_files):
        session_name = Path(file_path).stem
        df = pd.read_csv(file_path, comment="#")

        df.columns = [c.strip().lower() for c in df.columns]
        required = ["window_id", "sample_idx", "ax", "ay", "az", "temp_c", "label"]
        if not all(col in df.columns for col in required):
            print(f"[SKIP] {file_path} missing required columns")
            continue

        has_current = "current_a" in df.columns

        grouped = df.groupby("window_id", sort=True)
        prev_temp = None

        for win_id, group in grouped:
            if len(group) < WINDOW_SIZE:
                continue

            win_data = group.iloc[:WINDOW_SIZE]

            ax = win_data["ax"].values
            ay = win_data["ay"].values
            az = win_data["az"].values
            temp_val = float(win_data["temp_c"].iloc[0])

            current_val = None
            if has_current:

                c_vals = pd.to_numeric(win_data["current_a"], errors="coerce").fillna(0.0).values
                current_val = c_vals

            label = int(win_data["label"].mode()[0])

            feats = extract_window_features(
                ax=ax, ay=ay, az=az,
                temp_c=temp_val,
                current_a=current_val,
                temp_prev=prev_temp,
                delta_t_sec=1.28
            )
            prev_temp = temp_val

            all_features.append(feats)
            all_labels.append(label)
            all_sessions.append(session_name)
            window_ids.append(win_id)

    X = np.array(all_features, dtype=np.float32)
    y = np.array(all_labels, dtype=np.int32)
    sessions = np.array(all_sessions)

    print(f"[DATASET] Loaded {len(X)} windows from {len(set(sessions))} sessions.")
    for lbl in np.unique(y):
        count = np.sum(y == lbl)
        print(f"  Class {lbl}: {count} windows ({count * 1.28:.1f} s)")

    return X, y, sessions
