#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the untrained fully-INT8 TinyPose latency prototype assets."""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
os.environ.setdefault("TF_ENABLE_ONEDNN_OPTS", "0")
os.environ.setdefault("TF_NUM_INTRAOP_THREADS", "2")
os.environ.setdefault("TF_NUM_INTEROP_THREADS", "1")

import numpy as np
import tensorflow as tf


SEED = 34596
INPUT_SHAPE = (1, 96, 96, 3)
OUTPUT_SHAPE = (1, 24)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tensor_metadata(detail: dict) -> dict:
    scale, zero_point = detail["quantization"]
    return {
        "name": detail["name"],
        "shape": [int(value) for value in detail["shape"]],
        "dtype": np.dtype(detail["dtype"]).name,
        "scale": float(scale),
        "zero_point": int(zero_point),
    }


def build_model() -> tf.keras.Model:
    inputs = tf.keras.Input(batch_shape=INPUT_SHAPE, name="image", dtype=tf.float32)
    value = inputs
    for index, channels in enumerate((8, 16, 24, 32, 48), start=1):
        value = tf.keras.layers.Conv2D(
            channels,
            kernel_size=3,
            strides=2,
            padding="same",
            activation="relu",
            use_bias=True,
            kernel_initializer=tf.keras.initializers.GlorotUniform(
                seed=SEED + index
            ),
            bias_initializer="zeros",
            name=f"conv{index}_s2_c{channels}",
        )(value)
    value = tf.keras.layers.Flatten(name="flatten")(value)
    outputs = tf.keras.layers.Dense(
        24,
        kernel_initializer=tf.keras.initializers.GlorotUniform(seed=SEED + 6),
        bias_initializer="zeros",
        name="keypoints_8x3",
    )(value)
    return tf.keras.Model(inputs, outputs, name="tinypose_latency_prototype")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error("output must be a fresh directory")
    output.mkdir(parents=True)

    tf.keras.utils.set_random_seed(SEED)
    tf.config.experimental.enable_op_determinism()
    model = build_model()

    summary_buffer = io.StringIO()
    model.summary(print_fn=lambda line: summary_buffer.write(line + "\n"))
    (output / "model-summary.txt").write_text(summary_buffer.getvalue())

    calibration_rng = np.random.default_rng(SEED)
    calibration = [
        calibration_rng.uniform(-1.0, 1.0, INPUT_SHAPE).astype(np.float32)
        for _ in range(100)
    ]

    def representative_dataset():
        for sample in calibration:
            yield [sample]

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    model_bytes = converter.convert()
    model_path = output / "tinypose_int8.tflite"
    model_path.write_bytes(model_bytes)

    interpreter = tf.lite.Interpreter(
        model_content=model_bytes,
        num_threads=1,
        experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF,
    )
    interpreter.allocate_tensors()
    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    if tuple(input_detail["shape"]) != INPUT_SHAPE:
        raise RuntimeError(f"unexpected input shape: {input_detail['shape']}")
    if tuple(output_detail["shape"]) != OUTPUT_SHAPE:
        raise RuntimeError(f"unexpected output shape: {output_detail['shape']}")
    if input_detail["dtype"] != np.int8 or output_detail["dtype"] != np.int8:
        raise RuntimeError("model input/output must both be INT8")

    fixture_rng = np.random.default_rng(SEED + 1)
    fixture = fixture_rng.integers(-128, 128, INPUT_SHAPE, dtype=np.int16).astype(
        np.int8
    )
    interpreter.set_tensor(input_detail["index"], fixture)
    interpreter.invoke()
    oracle = interpreter.get_tensor(output_detail["index"]).astype(np.int8)
    (output / "fixture_int8.bin").write_bytes(fixture.tobytes())
    (output / "oracle_int8.bin").write_bytes(oracle.tobytes())

    ops = [
        entry["op_name"]
        for entry in interpreter._get_ops_details()
        if entry["op_name"] != "DELEGATE"
    ]
    expected_ops = ["CONV_2D"] * 5 + ["RESHAPE", "FULLY_CONNECTED"]
    if ops != expected_ops:
        raise RuntimeError(f"unexpected operator list: {ops}")

    metadata = {
        "status": "UNTRAINED_LATENCY_PROTOTYPE",
        "seed": SEED,
        "tensorflow": tf.__version__,
        "numpy": np.__version__,
        "network": [
            "Conv2D 3x3 stride2 C=8 fused ReLU SAME",
            "Conv2D 3x3 stride2 C=16 fused ReLU SAME",
            "Conv2D 3x3 stride2 C=24 fused ReLU SAME",
            "Conv2D 3x3 stride2 C=32 fused ReLU SAME",
            "Conv2D 3x3 stride2 C=48 fused ReLU SAME",
            "Flatten",
            "FullyConnected 24",
        ],
        "output_semantic": "8 keypoints x {x, y, confidence}; random weights",
        "operators": ops,
        "input": tensor_metadata(input_detail),
        "output": tensor_metadata(output_detail),
        "model_bytes": len(model_bytes),
        "model_sha256": sha256(model_path),
        "fixture_bytes": fixture.nbytes,
        "fixture_sha256": sha256(output / "fixture_int8.bin"),
        "oracle_bytes": oracle.nbytes,
        "oracle_sha256": sha256(output / "oracle_int8.bin"),
        "oracle_int8": [int(value) for value in oracle.ravel()],
        "latency_scope": {
            "included": "one complete TFLM Invoke per measured round",
            "excluded": [
                "camera capture",
                "resize",
                "RGB conversion",
                "render",
                "cold model init",
            ],
        },
    }
    (output / "metadata.json").write_text(
        json.dumps(metadata, indent=2, ensure_ascii=False) + "\n"
    )
    (output / "requirements.lock.txt").write_text(
        f"tensorflow-cpu=={tf.__version__}\nnumpy=={np.__version__}\n"
    )
    print(json.dumps(metadata, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
