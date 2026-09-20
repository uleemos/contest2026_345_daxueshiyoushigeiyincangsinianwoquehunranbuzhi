#!/usr/bin/env python3
"""Train and fully quantize the fixed TinyPose 8-keypoint network."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import pathlib

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
os.environ.setdefault("TF_ENABLE_ONEDNN_OPTS", "0")

import numpy as np
import tensorflow as tf

from keypoint_dataset import KEYPOINTS, load_jsonl, read_ppm


SEED = 34596
INPUT_SHAPE = (96, 96, 3)


def build_model(fixed_batch: bool = False) -> tf.keras.Model:
    if fixed_batch:
        inputs = tf.keras.Input(batch_shape=(1,) + INPUT_SHAPE,
                                name="image", dtype=tf.float32)
    else:
        inputs = tf.keras.Input(shape=INPUT_SHAPE, name="image", dtype=tf.float32)
    value = inputs
    for index, channels in enumerate((8, 16, 24, 32, 48), 1):
        value = tf.keras.layers.Conv2D(
            channels, 3, strides=2, padding="same", activation="relu",
            name=f"conv{index}_s2_c{channels}",
        )(value)
    value = tf.keras.layers.Flatten(name="flatten")(value)
    outputs = tf.keras.layers.Dense(24, name="keypoints_8x3")(value)
    return tf.keras.Model(inputs, outputs, name="tinypose_keypoints")


def pose_loss(expected: tf.Tensor, predicted: tf.Tensor) -> tf.Tensor:
    expected = tf.reshape(expected, (-1, 8, 3))
    predicted = tf.reshape(predicted, (-1, 8, 3))
    visibility = tf.cast(expected[..., 2] >= 0.20, tf.float32)
    coordinate_error = tf.reduce_sum(tf.square(expected[..., :2] - predicted[..., :2]), -1)
    coordinate_loss = tf.reduce_sum(coordinate_error * visibility) / tf.maximum(tf.reduce_sum(visibility), 1.0)
    confidence_loss = tf.reduce_mean(tf.square(expected[..., 2] - predicted[..., 2]))
    return coordinate_loss + 0.25 * confidence_loss


def load_records(paths: list[pathlib.Path], require_manual: bool = False,
                 binary_visibility_targets: bool = False) -> tuple[np.ndarray, np.ndarray, list[dict]]:
    images, labels, accepted = [], [], []
    for labels_path in paths:
        for record in load_jsonl(labels_path):
            if require_manual and record.get("label_source") != "manual":
                raise ValueError(f"validation record is not manual: {labels_path}")
            points = record["keypoints"]
            if [point["name"] for point in points] != list(KEYPOINTS):
                raise ValueError(f"keypoint order mismatch: {record['image']}")
            usable = sum(float(point["confidence"]) >= 0.20 for point in points)
            sample_kind = record.get("sample_kind")
            if sample_kind == "empty_background_negative":
                minimum_usable = 0
            elif sample_kind == "lower_body_cropped_negative":
                minimum_usable = 4
            else:
                minimum_usable = 6
            if usable < minimum_usable:
                continue
            image_path = labels_path.parent / record["image"]
            image = read_ppm(image_path)
            resized = tf.image.resize(image, INPUT_SHAPE[:2], method="area").numpy()
            images.append(resized / 127.5 - 1.0)
            labels.append([
                [point["x"], point["y"],
                 (1.0 if point.get("usable", float(point["confidence"]) >= 0.20)
                  else 0.0) if binary_visibility_targets else point["confidence"]]
                for point in points
            ])
            accepted.append(record)
    if not images:
        raise ValueError("no samples have at least six usable keypoints")
    return np.asarray(images, np.float32), np.asarray(labels, np.float32).reshape(-1, 24), accepted


def augment(images: np.ndarray, labels: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    flipped_images = images[:, :, ::-1, :]
    points = labels.reshape(-1, 8, 3).copy()
    points[..., 0] = 1.0 - points[..., 0]
    points = points[:, (1, 0, 3, 2, 5, 4, 7, 6), :]
    all_images = np.concatenate((images, flipped_images), axis=0)
    all_labels = np.concatenate((labels, points.reshape(-1, 24)), axis=0)
    return all_images, all_labels


def metadata(detail: dict) -> dict:
    scale, zero_point = detail["quantization"]
    return {"name": detail["name"], "shape": detail["shape"].astype(int).tolist(),
            "dtype": np.dtype(detail["dtype"]).name, "scale": float(scale),
            "zero_point": int(zero_point)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--train-labels", type=pathlib.Path, nargs="+", required=True)
    parser.add_argument("--validation-labels", type=pathlib.Path, nargs="+", required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--epochs", type=int, default=150)
    parser.add_argument("--initial-model", type=pathlib.Path,
                        help="optional float Keras checkpoint used to initialize tuning and refit")
    parser.add_argument("--learning-rate", type=float, default=2e-3)
    parser.add_argument("--binary-visibility-targets", action="store_true",
                        help="train confidence as visible=1 and hidden=0 using the usable label")
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output must be a fresh directory")
    args.output.mkdir(parents=True)

    tf.keras.utils.set_random_seed(SEED)
    train_x, train_y, train_records = load_records(
        args.train_labels, binary_visibility_targets=args.binary_visibility_targets)
    validation_x, validation_y, validation_records = load_records(
        args.validation_labels, binary_visibility_targets=args.binary_visibility_targets)
    original_train_x, original_train_y = train_x, train_y
    train_x, train_y = augment(train_x, train_y)
    if args.initial_model:
        model = tf.keras.models.load_model(args.initial_model, compile=False)
    else:
        model = build_model()
    model.compile(optimizer=tf.keras.optimizers.Adam(args.learning_rate), loss=pose_loss)
    callbacks = [
        tf.keras.callbacks.EarlyStopping(monitor="val_loss", patience=20, restore_best_weights=True),
        tf.keras.callbacks.ReduceLROnPlateau(monitor="val_loss", patience=7, factor=0.5, min_lr=1e-5),
    ]
    history = model.fit(train_x, train_y, validation_data=(validation_x, validation_y),
                        batch_size=min(32, len(train_x)), epochs=args.epochs,
                        callbacks=callbacks, verbose=2)
    best_epoch = int(np.argmin(history.history["val_loss"])) + 1

    # Freeze the epoch count on a separate tuning session, then refit a fresh
    # model on all non-holdout data. The independent manual acceptance set is
    # never passed to this script.
    refit_x = np.concatenate((original_train_x, validation_x), axis=0)
    refit_y = np.concatenate((original_train_y, validation_y), axis=0)
    refit_x, refit_y = augment(refit_x, refit_y)
    tf.keras.backend.clear_session()
    tf.keras.utils.set_random_seed(SEED)
    if args.initial_model:
        model = tf.keras.models.load_model(args.initial_model, compile=False)
    else:
        model = build_model()
    model.compile(optimizer=tf.keras.optimizers.Adam(args.learning_rate), loss=pose_loss)
    model.fit(refit_x, refit_y, batch_size=min(32, len(refit_x)),
              epochs=best_epoch, verbose=2)
    model.save(args.output / "tinypose_float.keras")
    export_model = build_model(fixed_batch=True)
    export_model.set_weights(model.get_weights())
    summary = io.StringIO()
    export_model.summary(print_fn=lambda line: summary.write(line + "\n"))
    (args.output / "model-summary.txt").write_text(summary.getvalue())

    def representative():
        for image in refit_x[: min(100, len(refit_x))]:
            yield [image[np.newaxis]]

    converter = tf.lite.TFLiteConverter.from_keras_model(export_model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    model_bytes = converter.convert()
    model_path = args.output / "tinypose_keypoints_int8.tflite"
    model_path.write_bytes(model_bytes)
    interpreter = tf.lite.Interpreter(model_content=model_bytes, num_threads=1,
        experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF)
    interpreter.allocate_tensors()
    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    ops = [item["op_name"] for item in interpreter._get_ops_details() if item["op_name"] != "DELEGATE"]
    expected = ["CONV_2D"] * 5 + ["RESHAPE", "FULLY_CONNECTED"]
    if ops != expected or input_detail["dtype"] != np.int8 or output_detail["dtype"] != np.int8:
        raise RuntimeError(f"unexpected fully INT8 model contract: {ops}")
    report = {
        "status": "TRAINED_KEYPOINT_MODEL",
        "keypoints": list(KEYPOINTS), "tuning_train_samples": len(train_records),
        "tuning_validation_samples": len(validation_records),
        "refit_samples": len(train_records) + len(validation_records),
        "augmented_refit_samples": len(refit_x),
        "tuning_epochs_completed": len(history.history["loss"]),
        "selected_refit_epochs": best_epoch,
        "best_tuning_validation_loss": float(min(history.history["val_loss"])),
        "initial_model": str(args.initial_model) if args.initial_model else None,
        "learning_rate": args.learning_rate,
        "binary_visibility_targets": args.binary_visibility_targets,
        "independent_holdout_used": False,
        "operators": ops, "input": metadata(input_detail), "output": metadata(output_detail),
        "model_bytes": len(model_bytes), "model_sha256": hashlib.sha256(model_bytes).hexdigest(),
    }
    (args.output / "training-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
