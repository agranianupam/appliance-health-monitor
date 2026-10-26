#pragma once
#include <Arduino.h>

#define NUM_CLASSES 3
#define NUM_TREES 10

static void predict_tree_0(const float* features, float* probs) {
  if (features[5] <= 0.516395f) {
    if (features[9] <= -0.696042f) {
      probs[0] += 1.00000f; probs[1] += 0.00000f; probs[2] += 0.00000f; return;
    } else {
      probs[0] += 0.00000f; probs[1] += 1.00000f; probs[2] += 0.00000f; return;
    }
  } else {
    probs[0] += 0.00000f; probs[1] += 0.00000f; probs[2] += 1.00000f; return;
  }
}

static void predict_tree_1(const float* features, float* probs) {
  if (features[10] <= 0.210245f) {
    if (features[12] <= -0.691849f) {
      probs[0] += 1.00000f; probs[1] += 0.00000f; probs[2] += 0.00000f; return;
    } else {
      probs[0] += 0.00000f; probs[1] += 1.00000f; probs[2] += 0.00000f; return;
    }
  } else {
    probs[0] += 0.00000f; probs[1] += 0.00000f; probs[2] += 1.00000f; return;
  }
}

static void predict_tree_2(const float* features, float* probs) {
  if (features[12] <= -0.682634f) {
    probs[0] += 1.00000f; probs[1] += 0.00000f; probs[2] += 0.00000f; return;
  } else {
    if (features[8] <= 0.012675f) {
      probs[0] += 0.00000f; probs[1] += 1.00000f; probs[2] += 0.00000f; return;
    } else {
      probs[0] += 0.00000f; probs[1] += 0.00000f; probs[2] += 1.00000f; return;
    }
  }
}

static void predict_tree_3(const float* features, float* probs) {
  if (features[2] <= -0.674217f) {
    probs[0] += 1.00000f; probs[1] += 0.00000f; probs[2] += 0.00000f; return;
  } else {
    if (features[14] <= 0.534508f) {
      probs[0] += 0.00000f; probs[1] += 1.00000f; probs[2] += 0.00000f; return;
    } else {
      probs[0] += 0.00000f; probs[1] += 0.00000f; probs[2] += 1.00000f; return;
    }
  }
}

static void predict_tree_4(const float* features, float* probs) {
  if (features[15] <= -0.705338f) {
    probs[0] += 1.00000f; probs[1] += 0.00000f; probs[2] += 0.00000f; return;
  } else {
    if (features[14] <= 0.534508f) {
      probs[0] += 0.00000f; probs[1] += 1.00000f; probs[2] += 0.00000f; return;
    } else {
      probs[0] += 0.00000f; probs[1] += 0.00000f; probs[2] += 1.00000f; return;
    }
  }
}

static void predict_tree_5(const float* features, float* probs) {
  if (features[15] <= -0.707106f) {
    probs[0] += 1.00000f; probs[1] += 0.00000f; probs[2] += 0.00000f; return;
  } else {
    if (features[15] <= 0.534470f) {
      probs[0] += 0.00000f; probs[1] += 1.00000f; probs[2] += 0.00000f; return;
    } else {
      probs[0] += 0.00000f; probs[1] += 0.00000f; probs[2] += 1.00000f; return;
    }
  }
}

static void predict_tree_6(const float* features, float* probs) {
  if (features[0] <= 0.463047f) {
    if (features[15] <= -0.707106f) {
      probs[0] += 1.00000f; probs[1] += 0.00000f; probs[2] += 0.00000f; return;
    } else {
      probs[0] += 0.00000f; probs[1] += 1.00000f; probs[2] += 0.00000f; return;
    }
  } else {
    probs[0] += 0.00000f; probs[1] += 0.00000f; probs[2] += 1.00000f; return;
  }
}

static void predict_tree_7(const float* features, float* probs) {
  if (features[10] <= -0.708224f) {
    probs[0] += 1.00000f; probs[1] += 0.00000f; probs[2] += 0.00000f; return;
  } else {
    if (features[3] <= 0.419586f) {
      probs[0] += 0.00000f; probs[1] += 1.00000f; probs[2] += 0.00000f; return;
    } else {
      probs[0] += 0.00000f; probs[1] += 0.00000f; probs[2] += 1.00000f; return;
    }
  }
}

static void predict_tree_8(const float* features, float* probs) {
  if (features[12] <= -0.684477f) {
    probs[0] += 1.00000f; probs[1] += 0.00000f; probs[2] += 0.00000f; return;
  } else {
    if (features[0] <= 0.463047f) {
      probs[0] += 0.00000f; probs[1] += 1.00000f; probs[2] += 0.00000f; return;
    } else {
      probs[0] += 0.00000f; probs[1] += 0.00000f; probs[2] += 1.00000f; return;
    }
  }
}

static void predict_tree_9(const float* features, float* probs) {
  if (features[4] <= -0.695789f) {
    probs[0] += 1.00000f; probs[1] += 0.00000f; probs[2] += 0.00000f; return;
  } else {
    if (features[5] <= 0.454592f) {
      probs[0] += 0.00000f; probs[1] += 1.00000f; probs[2] += 0.00000f; return;
    } else {
      probs[0] += 0.00000f; probs[1] += 0.00000f; probs[2] += 1.00000f; return;
    }
  }
}

static inline int predict_forest(const float* features, float* out_probs) {
  out_probs[0] = 0.0f;
  out_probs[1] = 0.0f;
  out_probs[2] = 0.0f;

  predict_tree_0(features, out_probs);
  predict_tree_1(features, out_probs);
  predict_tree_2(features, out_probs);
  predict_tree_3(features, out_probs);
  predict_tree_4(features, out_probs);
  predict_tree_5(features, out_probs);
  predict_tree_6(features, out_probs);
  predict_tree_7(features, out_probs);
  predict_tree_8(features, out_probs);
  predict_tree_9(features, out_probs);

  const float inv_trees = 1.0f / (float)10;
  out_probs[0] *= inv_trees;
  out_probs[1] *= inv_trees;
  out_probs[2] *= inv_trees;

  int best_class = 0;
  float max_prob = out_probs[0];
  if (out_probs[1] > max_prob) {
    best_class = 1;
    max_prob = out_probs[1];
  }
  if (out_probs[2] > max_prob) {
    best_class = 2;
  }
  return best_class;
}
