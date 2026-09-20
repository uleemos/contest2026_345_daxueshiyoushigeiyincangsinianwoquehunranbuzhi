#!/usr/bin/env python3
"""Evaluate fully INT8 TinyPose against independent manual labels."""

from __future__ import annotations

import argparse
import json
import pathlib

import numpy as np
import tensorflow as tf

from keypoint_dataset import KEYPOINTS, load_jsonl, read_ppm


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=pathlib.Path, required=True)
    parser.add_argument("--labels", type=pathlib.Path, nargs="+", required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    interpreter = tf.lite.Interpreter(model_path=str(args.model), num_threads=1)
    interpreter.allocate_tensors()
    inp, out = interpreter.get_input_details()[0], interpreter.get_output_details()[0]
    in_scale, in_zero = inp["quantization"]
    out_scale, out_zero = out["quantization"]
    errors, by_name = [], {name: [] for name in KEYPOINTS}
    images = 0
    for labels_path in args.labels:
        for record in load_jsonl(labels_path):
            if record.get("label_source") != "manual":
                raise ValueError("quality acceptance requires manual labels")
            image = read_ppm(labels_path.parent / record["image"])
            image = tf.image.resize(image, (96, 96), method="area").numpy() / 127.5 - 1.0
            quantized = np.clip(np.rint(image / in_scale + in_zero), -128, 127).astype(np.int8)[None]
            interpreter.set_tensor(inp["index"], quantized)
            interpreter.invoke()
            prediction = (interpreter.get_tensor(out["index"]).astype(np.float32) - out_zero) * out_scale
            prediction = prediction.reshape(8, 3)
            images += 1
            for index, point in enumerate(record["keypoints"]):
                if float(point["confidence"]) < 0.5:
                    continue
                dx = prediction[index, 0] - float(point["x"])
                dy = prediction[index, 1] - float(point["y"])
                error = float(np.hypot(dx, dy) * 96.0)
                errors.append(error)
                by_name[KEYPOINTS[index]].append(error)
    if not errors:
        raise ValueError("manual holdout contains no visible keypoints")
    array = np.asarray(errors)
    report = {
        "model": str(args.model), "manual_holdout_images": images,
        "visible_keypoints": len(errors), "normalization": "96x96 image pixels",
        "mean_error_px": float(array.mean()), "median_error_px": float(np.median(array)),
        "p95_error_px": float(np.percentile(array, 95)),
        "pck_5px": float(np.mean(array <= 5.0)), "pck_10px": float(np.mean(array <= 10.0)),
        "per_keypoint_mean_error_px": {name: (float(np.mean(values)) if values else None) for name, values in by_name.items()},
        "provisional_gate": {"median_error_lt_5px": bool(np.median(array) < 5.0),
                             "pck_10px_ge_0_85": bool(np.mean(array <= 10.0) >= 0.85)},
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
