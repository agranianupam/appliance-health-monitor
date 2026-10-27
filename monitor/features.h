#pragma once

#include <math.h>

#define FEATURE_WINDOW_SIZE 128
#define GRAVITY_MSS 9.80665f

// ACS712 5A via ADS1115 (A0) with 22k/10k voltage divider
#define CURRENT_VOLTAGE_DIVIDER_RATIO 3.2f       // (22k + 10k) / 10k = 3.20
#define CURRENT_ACS712_SENSITIVITY    0.185f      // 185 mV/A (0.185 V/A) for ACS712-05B
#define CURRENT_NOMINAL_ZERO_V        2.50f      // Nominal quiescent midpoint voltage (Vcc/2)
#define CURRENT_NOISE_DEADBAND_A      0.025f     // Clamps noise below 25 mA to 0.000 A

static inline float compute_acs712_current(float v_adc, float v_zero, float divider_ratio, float sensitivity, float deadband) {
  float v_sensor = v_adc * divider_ratio;
  float delta_v = v_sensor - v_zero;
  float amps = fabsf(delta_v) / sensitivity;
  if (amps < deadband) {
    amps = 0.0f;
  }
  return amps;
}

static inline void fft_128_radix2(float* real, float* imag) {
  int j = 0;
  for (int i = 0; i < 127; i++) {
    if (i < j) {
      float tr = real[i]; real[i] = real[j]; real[j] = tr;
      float ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
    }
    int k = 64;
    while (k <= j) {
      j -= k;
      k >>= 1;
    }
    j += k;
  }

  for (int stage = 1; stage <= 7; stage++) {
    int m = 1 << stage;
    int half_m = m >> 1;
    float theta = -2.0f * (float)M_PI / (float)m;
    float w_step_re = cosf(theta);
    float w_step_im = sinf(theta);

    for (int k = 0; k < 128; k += m) {
      float w_re = 1.0f;
      float w_im = 0.0f;
      for (int i = 0; i < half_m; i++) {
        int idx_u = k + i;
        int idx_v = k + i + half_m;

        float u_re = real[idx_u];
        float u_im = imag[idx_u];

        float v_re = real[idx_v] * w_re - imag[idx_v] * w_im;
        float v_im = real[idx_v] * w_im + imag[idx_v] * w_re;

        real[idx_u] = u_re + v_re;
        imag[idx_u] = u_im + v_im;
        real[idx_v] = u_re - v_re;
        imag[idx_v] = u_im - v_im;

        float next_w_re = w_re * w_step_re - w_im * w_step_im;
        float next_w_im = w_re * w_step_im + w_im * w_step_re;
        w_re = next_w_re;
        w_im = next_w_im;
      }
    }
  }
}

static inline void compute_vibration_metrics(
    const float* ax,
    const float* ay,
    const float* az,
    float* out_vib_rms,
    float* out_bands,
    float* out_dom_freq
) {
  float mag[128];
  float sum_mag = 0.0f;

  for (int i = 0; i < 128; i++) {
    float m = sqrtf(ax[i] * ax[i] + ay[i] * ay[i] + az[i] * az[i]);
    mag[i] = m;
    sum_mag += m;
  }

  float mean_mag = sum_mag / 128.0f;
  float sum_sq = 0.0f;
  float re[128];
  float im[128];

  for (int i = 0; i < 128; i++) {
    float d = mag[i] - mean_mag;
    sum_sq += d * d;
    re[i] = d;
    im[i] = 0.0f;
  }

  *out_vib_rms = sqrtf(sum_sq / 128.0f);

  fft_128_radix2(re, im);

  float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
  const float inv_n = 1.0f / 128.0f;

  for (int k = 2; k <= 12; k++) {
    b0 += (re[k]*re[k] + im[k]*im[k]) * inv_n;
  }
  for (int k = 13; k <= 31; k++) {
    b1 += (re[k]*re[k] + im[k]*im[k]) * inv_n;
  }
  for (int k = 32; k <= 51; k++) {
    b2 += (re[k]*re[k] + im[k]*im[k]) * inv_n;
  }
  for (int k = 52; k <= 64; k++) {
    b3 += (re[k]*re[k] + im[k]*im[k]) * inv_n;
  }

  out_bands[0] = b0;
  out_bands[1] = b1;
  out_bands[2] = b2;
  out_bands[3] = b3;

  float max_p = 0.0f;
  int max_k = 2;
  for (int k = 2; k <= 64; k++) {
    float p = (re[k]*re[k] + im[k]*im[k]) * inv_n;
    if (p > max_p) {
      max_p = p;
      max_k = k;
    }
  }

  *out_dom_freq = (float)max_k * 0.78125f;
}

static inline void extract_features(
    const float* ax,
    const float* ay,
    const float* az,
    float temp_c,
    float temp_prev,
    float current_a,
    float* out_features
) {
  float mag[128];
  float dyn[128];
  float sum_mag = 0.0f;
  float sum_ax = 0.0f, sum_ay = 0.0f, sum_az = 0.0f;
  float min_x = ax[0], max_x = ax[0];
  float min_y = ay[0], max_y = ay[0];
  float min_z = az[0], max_z = az[0];

  for (int i = 0; i < 128; i++) {
    float m = sqrtf(ax[i] * ax[i] + ay[i] * ay[i] + az[i] * az[i]);
    mag[i] = m;
    dyn[i] = m - GRAVITY_MSS;
    sum_mag += m;
    sum_ax += ax[i];
    sum_ay += ay[i];
    sum_az += az[i];
    if (ax[i] < min_x) min_x = ax[i];
    if (ax[i] > max_x) max_x = ax[i];
    if (ay[i] < min_y) min_y = ay[i];
    if (ay[i] > max_y) max_y = ay[i];
    if (az[i] < min_z) min_z = az[i];
    if (az[i] > max_z) max_z = az[i];
  }

  // 1. vib_rms of dynamic acceleration
  float sum_dyn_sq = 0.0f;
  float peak_dyn = 0.0f;
  for (int i = 0; i < 128; i++) {
    sum_dyn_sq += dyn[i] * dyn[i];
    float abs_d = fabsf(dyn[i]);
    if (abs_d > peak_dyn) peak_dyn = abs_d;
  }
  float vib_rms = sqrtf(sum_dyn_sq / 128.0f);
  out_features[0] = vib_rms;

  // 2. per-axis standard deviation
  float mean_x = sum_ax / 128.0f;
  float mean_y = sum_ay / 128.0f;
  float mean_z = sum_az / 128.0f;
  float sum_x_sq = 0.0f, sum_y_sq = 0.0f, sum_z_sq = 0.0f;
  for (int i = 0; i < 128; i++) {
    float dx = ax[i] - mean_x; sum_x_sq += dx * dx;
    float dy = ay[i] - mean_y; sum_y_sq += dy * dy;
    float dz = az[i] - mean_z; sum_z_sq += dz * dz;
  }
  out_features[1] = sqrtf(sum_x_sq / 128.0f);
  out_features[2] = sqrtf(sum_y_sq / 128.0f);
  out_features[3] = sqrtf(sum_z_sq / 128.0f);

  // 3. per-axis peak-to-peak
  out_features[4] = max_x - min_x;
  out_features[5] = max_y - min_y;
  out_features[6] = max_z - min_z;

  // 4. crest factor
  out_features[7] = peak_dyn / (vib_rms + 1e-5f);

  // 5. 4 FFT band energies on dyn_accel (zero-mean)
  float dyn_mean = 0.0f;
  for (int i = 0; i < 128; i++) dyn_mean += dyn[i];
  dyn_mean /= 128.0f;

  float re[128], im[128];
  for (int i = 0; i < 128; i++) {
    re[i] = dyn[i] - dyn_mean;
    im[i] = 0.0f;
  }
  fft_128_radix2(re, im);

  const float inv_n = 1.0f / 128.0f;
  float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
  for (int k = 2; k <= 12; k++) b0 += (re[k]*re[k] + im[k]*im[k]) * inv_n;
  for (int k = 13; k <= 31; k++) b1 += (re[k]*re[k] + im[k]*im[k]) * inv_n;
  for (int k = 32; k <= 51; k++) b2 += (re[k]*re[k] + im[k]*im[k]) * inv_n;
  for (int k = 52; k <= 64; k++) b3 += (re[k]*re[k] + im[k]*im[k]) * inv_n;

  out_features[8] = b0;
  out_features[9] = b1;
  out_features[10] = b2;
  out_features[11] = b3;

  // 6. Temperature mean & slope (deg C per min)
  out_features[12] = temp_c;
  out_features[13] = (temp_c - temp_prev) / 1.28f * 60.0f;

  // 7. Current mean & RMS
  out_features[14] = current_a;
  out_features[15] = current_a;
}
