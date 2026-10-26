"""
Feature Extraction for ESP32 Predictive Maintenance.
Computes identical features in Python and C++:
  1. Vibration RMS of dynamic acceleration (magnitude - gravity)
  2. Per-axis standard deviation (std_x, std_y, std_z)
  3. Per-axis peak-to-peak (p2p_x, p2p_y, p2p_z)
  4. Dynamic acceleration crest factor
  5. 4 FFT band energies:
       - Band 0: 1 - 10 Hz (sub-synchronous / structural looseness)
       - Band 1: 10 - 25 Hz (low rotational speed range)
       - Band 2: 25 - 40 Hz (primary fan 1X RPM imbalance range)
       - Band 3: 40 - 50 Hz (high frequency / bearing / aerodynamic turbulence)
  6. Temperature value (mean) & slope (deg C per minute)
  7. Current mean & RMS (if enabled)
"""

import numpy as np

GRAVITY = 9.80665
FS = 100.0
WINDOW_SIZE = 128

FEATURE_NAMES = [
    "vib_rms",
    "std_x", "std_y", "std_z",
    "p2p_x", "p2p_y", "p2p_z",
    "crest_factor",
    "fft_band_0", "fft_band_1", "fft_band_2", "fft_band_3",
    "temp_mean", "temp_slope_per_min",
    "current_mean", "current_rms"
]

def compute_fft_band_energies(signal, fs=100.0, n_fft=128):
    """
    Computes energy in 4 frequency bands from rfft:
      Band 0:  1 Hz <= f < 10 Hz
      Band 1: 10 Hz <= f < 25 Hz
      Band 2: 25 Hz <= f < 40 Hz
      Band 3: 40 Hz <= f <= 50 Hz
    """

    signal_zero_mean = signal - np.mean(signal)

    fft_vals = np.fft.rfft(signal_zero_mean, n=n_fft)
    freqs = np.fft.rfftfreq(n_fft, d=1.0 / fs)

    power = (np.abs(fft_vals) ** 2) / n_fft

    b0_mask = (freqs >= 1.0) & (freqs < 10.0)
    b1_mask = (freqs >= 10.0) & (freqs < 25.0)
    b2_mask = (freqs >= 25.0) & (freqs < 40.0)
    b3_mask = (freqs >= 40.0) & (freqs <= 50.0)

    e0 = float(np.sum(power[b0_mask])) if np.any(b0_mask) else 0.0
    e1 = float(np.sum(power[b1_mask])) if np.any(b1_mask) else 0.0
    e2 = float(np.sum(power[b2_mask])) if np.any(b2_mask) else 0.0
    e3 = float(np.sum(power[b3_mask])) if np.any(b3_mask) else 0.0

    return e0, e1, e2, e3

def extract_window_features(ax, ay, az, temp_c, current_a=None, temp_prev=None, delta_t_sec=1.28):
    """
    Extracts fixed 16-feature vector from a 128-sample window.
    ax, ay, az: arrays of 128 acceleration samples in m/s^2.
    temp_c: temperature reading for the window.
    temp_prev: temperature from previous window (to compute slope).
    current_a: array or scalar current in Amps (or None/0.0).
    """
    ax = np.asarray(ax, dtype=np.float32)
    ay = np.asarray(ay, dtype=np.float32)
    az = np.asarray(az, dtype=np.float32)

    mag = np.sqrt(ax**2 + ay**2 + az**2)
    dyn_accel = mag - GRAVITY

    vib_rms = float(np.sqrt(np.mean(dyn_accel**2)))

    std_x = float(np.std(ax))
    std_y = float(np.std(ay))
    std_z = float(np.std(az))

    p2p_x = float(np.ptp(ax))
    p2p_y = float(np.ptp(ay))
    p2p_z = float(np.ptp(az))

    peak_dyn = float(np.max(np.abs(dyn_accel)))
    crest_factor = float(peak_dyn / (vib_rms + 1e-5))

    e0, e1, e2, e3 = compute_fft_band_energies(dyn_accel, fs=FS, n_fft=WINDOW_SIZE)

    temp_mean = float(np.mean(temp_c)) if hasattr(temp_c, "__len__") else float(temp_c)
    if temp_prev is not None and delta_t_sec > 0:

        temp_slope_per_min = float((temp_mean - temp_prev) / delta_t_sec * 60.0)
    else:
        temp_slope_per_min = 0.0

    if current_a is not None:
        curr_arr = np.asarray(current_a, dtype=np.float32)

        valid = ~np.isnan(curr_arr)
        if np.any(valid):
            curr_mean = float(np.mean(curr_arr[valid]))
            curr_rms = float(np.sqrt(np.mean(curr_arr[valid]**2)))
        else:
            curr_mean = 0.0
            curr_rms = 0.0
    else:
        curr_mean = 0.0
        curr_rms = 0.0

    features = [
        vib_rms,
        std_x, std_y, std_z,
        p2p_x, p2p_y, p2p_z,
        crest_factor,
        e0, e1, e2, e3,
        temp_mean, temp_slope_per_min,
        curr_mean, curr_rms
    ]
    return np.array(features, dtype=np.float32)
