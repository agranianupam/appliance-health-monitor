#pragma once
#define MODEL_NUM_FEATURES 16

static const float SCALER_MEANS[16] = {
  0.523988f, 0.909461f, 0.764713f, 0.499569f, 3.696904f, 3.127406f, 2.175931f, 2.440792f, 1.012796f, 1.825222f, 21.766098f, 1.238032f, 34.273633f, 3.165193f, 0.223546f, 0.223606f
};

static const float SCALER_SCALES[16] = {
  0.401602f, 0.713785f, 0.591007f, 0.372313f, 2.761834f, 2.304910f, 1.553132f, 0.248574f, 1.153933f, 2.064656f, 26.217281f, 1.446106f, 5.425505f, 4.306836f, 0.067141f, 0.067126f
};

static inline void standardize_features(const float* raw, float* out) {
  for (int i = 0; i < 16; i++) {
    out[i] = (raw[i] - SCALER_MEANS[i]) / SCALER_SCALES[i];
  }
}
