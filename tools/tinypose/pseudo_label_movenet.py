#!/usr/bin/env python3
"""Create eight-keypoint TinyPose pseudo-labels with the MoveNet teacher."""

from __future__ import annotations

import argparse
import hashlib
import pathlib

import numpy as np
import tensorflow as tf

from keypoint_dataset import KEYPOINTS, MOVENET_INDICES, load_jsonl, read_ppm, write_jsonl


def quantize(image: np.ndarray, detail: dict) -> np.ndarray:
    scale, zero_point = detail["quantization"]
    dtype = detail["dtype"]
    if dtype == np.uint8 and scale == 0:
        return image[np.newaxis]
    if scale <= 0:
        raise ValueError(f"unsupported teacher input quantization: {detail}")
    real = image.astype(np.float32)
    values = np.rint(real / scale + zero_point)
    info = np.iinfo(dtype)
    return np.clip(values, info.min, info.max).astype(dtype)[np.newaxis]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=pathlib.Path, required=True)
    parser.add_argument("--model", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--min-confidence", type=float, default=0.20)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output must not already exist")

    interpreter = tf.lite.Interpreter(model_path=str(args.model), num_threads=2)
    interpreter.allocate_tensors()
    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    records = []
    for item in load_jsonl(args.dataset / "manifest.jsonl"):
        image = read_ppm(args.dataset / item["image"])
        if tuple(image.shape) != (192, 192, 3):
            raise ValueError(f"teacher requires 192x192 RGB: {item['image']}")
        interpreter.set_tensor(input_detail["index"], quantize(image, input_detail))
        interpreter.invoke()
        output = interpreter.get_tensor(output_detail["index"]).reshape(17, 3)
        points = []
        for name, teacher_index in zip(KEYPOINTS, MOVENET_INDICES):
            y, x, confidence = (float(value) for value in output[teacher_index])
            points.append(
                {
                    "name": name,
                    "x": min(1.0, max(0.0, x)),
                    "y": min(1.0, max(0.0, y)),
                    "confidence": min(1.0, max(0.0, confidence)),
                    "usable": confidence >= args.min_confidence,
                }
            )
        records.append({**item, "keypoints": points, "label_source": "movenet_teacher"})
    write_jsonl(args.output, records)
    digest = hashlib.sha256(args.model.read_bytes()).hexdigest()
    print(f"PASS images={len(records)} teacher_sha256={digest} output={args.output}")


if __name__ == "__main__":
    main()
