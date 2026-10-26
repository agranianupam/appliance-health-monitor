"""
Model Training and ESP32 TinyML Export.
1. Trains Random Forest / Decision Tree and small Dense Neural Network (<10k params).
2. Evaluates via GroupKFold (split strictly by session).
3. Handles class imbalance.
4. Generates C++ inference headers for ESP32:
   - model_scaler.h (standardization mean & scale)
   - model_tree.h (zero-dependency C++ random forest inference)
   - model_tflite.h (quantized TFLite flatbuffer C array)
"""

import os
import json
import numpy as np
from pathlib import Path
from sklearn.model_selection import StratifiedGroupKFold
from sklearn.preprocessing import StandardScaler
from sklearn.ensemble import RandomForestClassifier
from sklearn.tree import DecisionTreeClassifier
from sklearn.metrics import classification_report, confusion_matrix, accuracy_score

from dataset import load_and_process_dataset
from features import FEATURE_NAMES

import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

CLASS_NAMES = ["Normal", "Warning", "Critical"]

def export_scaler_cpp(scaler, output_path="monitor/model_scaler.h"):
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    means = scaler.mean_
    scales = scaler.scale_
    n_feats = len(means)

    lines = [
        "// Auto-generated StandardScaler parameters for ESP32",
        "#pragma once",
        f"#define MODEL_NUM_FEATURES {n_feats}",
        "",
        f"static const float SCALER_MEANS[{n_feats}] = {{",
        "  " + ", ".join(f"{m:.6f}f" for m in means),
        "};",
        "",
        f"static const float SCALER_SCALES[{n_feats}] = {{",
        "  " + ", ".join(f"{s:.6f}f" for s in scales),
        "};",
        "",
        "static inline void standardize_features(const float* raw, float* out) {",
        f"  for (int i = 0; i < {n_feats}; i++) {{",
        "    out[i] = (raw[i] - SCALER_MEANS[i]) / SCALER_SCALES[i];",
        "  }",
        "}",
        ""
    ]
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"[EXPORT] Saved scaler to {output_path}")

def export_tree_cpp(clf, output_path="monitor/model_tree.h"):
    """
    Exports scikit-learn DecisionTreeClassifier or RandomForestClassifier
    as pure, dependency-free C++ code that compiles on any Arduino/ESP32 target.
    """
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    if isinstance(clf, DecisionTreeClassifier):
        trees = [clf]
    else:
        trees = clf.estimators_

    n_trees = len(trees)
    lines = [
        "// Auto-generated TinyML Random Forest / Decision Tree Classifier for ESP32",
        "// Pure C++ implementation with zero external library dependencies",
        "#pragma once",
        "#include <Arduino.h>",
        "",
        f"#define NUM_CLASSES 3",
        f"#define NUM_TREES {n_trees}",
        ""
    ]

    def render_node(tree, node_id, indent="  "):
        feature = tree.tree_.feature[node_id]
        threshold = tree.tree_.threshold[node_id]
        left = tree.tree_.children_left[node_id]
        right = tree.tree_.children_right[node_id]

        if left == -1 and right == -1:
            val = tree.tree_.value[node_id][0]

            total = np.sum(val)
            probs = val / (total + 1e-9)
            return [f"{indent}probs[0] += {probs[0]:.5f}f; probs[1] += {probs[1]:.5f}f; probs[2] += {probs[2]:.5f}f; return;"]

        code = []
        code.append(f"{indent}if (features[{feature}] <= {threshold:.6f}f) {{")
        code.extend(render_node(tree, left, indent + "  "))
        code.append(f"{indent}}} else {{")
        code.extend(render_node(tree, right, indent + "  "))
        code.append(f"{indent}}}")
        return code

    for i, t in enumerate(trees):
        lines.append(f"static void predict_tree_{i}(const float* features, float* probs) {{")
        lines.extend(render_node(t, 0, "  "))
        lines.append("}\n")

    lines.append("""
// Returns predicted class (0, 1, or 2) and writes class probabilities to out_probs
static inline int predict_forest(const float* features, float* out_probs) {
  out_probs[0] = 0.0f;
  out_probs[1] = 0.0f;
  out_probs[2] = 0.0f;
""")
    for i in range(n_trees):
        lines.append(f"  predict_tree_{i}(features, out_probs);")

    lines.append(f"""
  // Average across all trees
  const float inv_trees = 1.0f / (float){n_trees};
  out_probs[0] *= inv_trees;
  out_probs[1] *= inv_trees;
  out_probs[2] *= inv_trees;

  // Majority vote argmax
  int best_class = 0;
  float max_prob = out_probs[0];
  if (out_probs[1] > max_prob) {{
    best_class = 1;
    max_prob = out_probs[1];
  }}
  if (out_probs[2] > max_prob) {{
    best_class = 2;
  }}
  return best_class;
}}
""")

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"[EXPORT] Saved C++ tree classifier to {output_path}")

def build_neural_net(input_dim=16, num_classes=3):
    """
    Builds a small dense neural network with under 1,000 parameters.
    """
    model = keras.Sequential([
        layers.Input(shape=(input_dim,)),
        layers.Dense(16, activation="relu"),
        layers.Dense(8, activation="relu"),
        layers.Dense(num_classes, activation="softmax")
    ])
    model.compile(
        optimizer=keras.optimizers.Adam(learning_rate=0.005),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"]
    )
    return model

def export_tflite_cpp(model, X_train, output_path="monitor/model_tflite.h"):
    """
    Converts Keras model to int8 quantized TFLite and writes C array header.
    """
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    def representative_dataset():
        for i in range(min(100, len(X_train))):
            yield [X_train[i:i+1].astype(np.float32)]

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8

    try:
        tflite_quant = converter.convert()
    except Exception as e:
        print(f"[WARN] Full int8 quantization fallback to dynamic range: {e}")
        converter = tf.lite.TFLiteConverter.from_keras_model(model)
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        tflite_quant = converter.convert()

    tflite_size_bytes = len(tflite_quant)
    print(f"[TFLITE] Quantized model size: {tflite_size_bytes} bytes ({tflite_size_bytes / 1024:.2f} KB)")

    hex_lines = []
    for i in range(0, len(tflite_quant), 12):
        chunk = tflite_quant[i:i+12]
        hex_lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")

    c_content = [
        "// Auto-generated TensorFlow Lite Micro int8 model header for ESP32",
        "#pragma once",
        "#include <stddef.h>",
        "",
        f"constexpr unsigned int g_model_len = {len(tflite_quant)};",
        "alignas(16) const unsigned char g_model[] = {",
        "\n".join(hex_lines),
        "};",
        ""
    ]
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(c_content))
    print(f"[EXPORT] Saved TFLite model array to {output_path}")

def main():
    print("=== Step 3: TinyML Feature Training & Evaluation ===")
    X, y, sessions = load_and_process_dataset("data/raw")

    sgkf = StratifiedGroupKFold(n_splits=3)

    print(f"\n--- Running 3-Fold Stratified Grouped Cross-Validation (Strict Session Split) ---")

    rf_accs = []
    nn_accs = []
    all_y_true = []
    all_rf_preds = []

    for fold, (train_idx, test_idx) in enumerate(sgkf.split(X, y, groups=sessions)):
        X_tr, y_tr = X[train_idx], y[train_idx]
        X_te, y_te = X[test_idx], y[test_idx]

        scaler_fold = StandardScaler()
        X_tr_s = scaler_fold.fit_transform(X_tr)
        X_te_s = scaler_fold.transform(X_te)

        rf = RandomForestClassifier(
            n_estimators=10,
            max_depth=5,
            class_weight="balanced",
            random_state=42 + fold
        )
        rf.fit(X_tr_s, y_tr)
        rf_pred = rf.predict(X_te_s)
        rf_acc = accuracy_score(y_te, rf_pred)
        rf_accs.append(rf_acc)

        all_y_true.extend(y_te)
        all_rf_preds.extend(rf_pred)

        nn = build_neural_net(input_dim=X.shape[1], num_classes=3)

        classes, counts = np.unique(y_tr, return_counts=True)
        total_samples = len(y_tr)
        cw = {c: total_samples / (len(classes) * cnt) for c, cnt in zip(classes, counts)}

        nn.fit(
            X_tr_s, y_tr,
            epochs=25,
            batch_size=16,
            class_weight=cw,
            verbose=0
        )
        nn_pred = np.argmax(nn.predict(X_te_s, verbose=0), axis=1)
        nn_acc = accuracy_score(y_te, nn_pred)
        nn_accs.append(nn_acc)

        print(f"  Fold {fold+1}: RF Accuracy = {rf_acc*100:.2f}%, NN Accuracy = {nn_acc*100:.2f}%")

    print(f"\n[EVALUATION SUMMARY]")
    print(f"  Random Forest Mean Accuracy:   {np.mean(rf_accs)*100:.2f}% (+/- {np.std(rf_accs)*100:.2f}%)")
    print(f"  Neural Network Mean Accuracy:  {np.mean(nn_accs)*100:.2f}% (+/- {np.std(nn_accs)*100:.2f}%)")

    print("\n--- Classification Report (Random Forest) ---")
    print(classification_report(all_y_true, all_rf_preds, target_names=CLASS_NAMES, digits=4))

    print("--- Confusion Matrix (Random Forest) ---")
    cm = confusion_matrix(all_y_true, all_rf_preds)
    print(cm)

    print("\n--- Training Final Models on Full Dataset ---")
    scaler_full = StandardScaler()
    X_full_s = scaler_full.fit_transform(X)

    final_rf = RandomForestClassifier(
        n_estimators=10,
        max_depth=5,
        class_weight="balanced",
        random_state=42
    )
    final_rf.fit(X_full_s, y)

    final_nn = build_neural_net(input_dim=X.shape[1], num_classes=3)
    final_nn.fit(X_full_s, y, epochs=30, batch_size=16, verbose=0)

    scaler_params = {
        "features": FEATURE_NAMES,
        "means": scaler_full.mean_.tolist(),
        "scales": scaler_full.scale_.tolist()
    }
    with open("train/scaler_params.json", "w", encoding="utf-8") as f:
        json.dump(scaler_params, f, indent=2)
    print("[EXPORT] Saved train/scaler_params.json")

    export_scaler_cpp(scaler_full, output_path="monitor/model_scaler.h")
    export_tree_cpp(final_rf, output_path="monitor/model_tree.h")
    export_tflite_cpp(final_nn, X_full_s, output_path="monitor/model_tflite.h")

    print("\n[SUCCESS] Step 3 & 4 Training & Model Export Complete.")

if __name__ == "__main__":
    main()
