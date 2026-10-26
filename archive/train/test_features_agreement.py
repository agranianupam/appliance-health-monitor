"""
Verification Test: Checks that Python train/features.py and ESP32 C++ monitor/features.h
produce identical feature vectors on the same recorded 128-sample sensor window.
"""

import os
import subprocess
import pandas as pd
import numpy as np
from pathlib import Path
from features import extract_window_features, FEATURE_NAMES

def main():
    print("=== Step 4 Test: Feature Agreement between Python and C++ ===")

    cpp_src = Path("train/verify_features.cpp")
    bin_out = Path("train/verify_features.exe")
    cmd = ["g++", "-O2", str(cpp_src), "-o", str(bin_out)]
    print(f"[BUILD] Compiling {cpp_src} -> {bin_out}...")
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[ERROR] Compilation failed:\n{res.stderr}")
        return False

    raw_files = list(Path("data/raw").glob("*.csv"))
    if not raw_files:
        print("[ERROR] No raw session CSV files found in data/raw/")
        return False

    sample_csv = raw_files[0]
    df = pd.read_csv(sample_csv)

    w0 = df[df["window_id"] == 0].iloc[:128]
    if len(w0) < 128:
        print("[ERROR] Window has fewer than 128 samples")
        return False

    temp_window_csv = Path("train/temp_test_window.csv")
    w0.to_csv(temp_window_csv, index=False)

    ax = w0["ax"].values
    ay = w0["ay"].values
    az = w0["az"].values
    temp_c = float(w0["temp_c"].iloc[0])
    current_a = pd.to_numeric(w0["current_a"], errors="coerce").fillna(0.0).values

    py_feats = extract_window_features(
        ax=ax, ay=ay, az=az,
        temp_c=temp_c,
        current_a=current_a,
        temp_prev=temp_c,
        delta_t_sec=1.28
    )

    run_res = subprocess.run([str(bin_out), str(temp_window_csv)], capture_output=True, text=True)
    if run_res.returncode != 0:
        print(f"[ERROR] C++ runner failed:\n{run_res.stderr}")
        return False

    cpp_out_str = run_res.stdout.strip()
    cpp_feats = np.array([float(x) for x in cpp_out_str.split(",")], dtype=np.float32)

    if temp_window_csv.exists():
        temp_window_csv.unlink()

    print("\n-------------------------------------------------------------------------")
    print(f"{'Feature Name':<22} | {'Python':>12} | {'C++ (ESP32)':>12} | {'Diff':>10} | {'Status'}")
    print("-------------------------------------------------------------------------")

    all_matched = True
    for name, py_v, cpp_v in zip(FEATURE_NAMES, py_feats, cpp_feats):
        diff = abs(py_v - cpp_v)

        passed = diff < 0.005
        status_str = "PASS" if passed else "FAIL"
        if not passed:
            all_matched = False
        print(f"{name:<22} | {py_v:>12.5f} | {cpp_v:>12.5f} | {diff:>10.6f} | {status_str}")

    print("-------------------------------------------------------------------------")
    if all_matched:
        print("[SUCCESS] All 16 features agree between Python and ESP32 C++ implementation!\n")
        return True
    else:
        print("[FAILURE] Feature discrepancy detected between Python and C++!\n")
        return False

if __name__ == "__main__":
    main()
