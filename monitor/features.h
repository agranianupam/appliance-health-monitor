#pragma once

#include <math.h>

#define FEATURE_WINDOW_SIZE 128
#define GRAVITY_MSS 9.80665f

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
