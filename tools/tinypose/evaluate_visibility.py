#!/usr/bin/env python3
"""Evaluate TinyPose visibility confidence against manual labels."""

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
    parser.add_argument("--threshold", type=float, default=0.30)
    args = parser.parse_args()

    interpreter = tf.lite.Interpreter(model_path=str(args.model), num_threads=1)
    interpreter.allocate_tensors()
    inp = interpreter.get_input_details()[0]
    out = interpreter.get_output_details()[0]
    in_scale, in_zero = inp["quantization"]
    out_scale, out_zero = out["quantization"]
    visible_total = hidden_total = visible_accept = hidden_reject = 0
    images = all_required_visible = confidence_gate_reject = 0
    by_name = {name: {"visible": [], "hidden": []} for name in KEYPOINTS}

    for labels_path in args.labels:
        for record in load_jsonl(labels_path):
            if record.get("label_source") != "manual":
                raise ValueError("visibility evaluation requires manual labels")
            image = read_ppm(labels_path.parent / record["image"])
            image = tf.image.resize(image, (96, 96), method="area").numpy()
            image = image / 127.5 - 1.0
            quantized = np.clip(np.rint(image / in_scale + in_zero),
                                -128, 127).astype(np.int8)[None]
            interpreter.set_tensor(inp["index"], quantized)
            interpreter.invoke()
            values = ((interpreter.get_tensor(out["index"]).astype(np.float32)
                       - out_zero) * out_scale).reshape(8, 3)
            scores = np.clip(values[:, 2], 0.0, 1.0)
            expected = np.asarray([float(point["confidence"]) >= 0.5
                                   for point in record["keypoints"]])
            accepted = scores >= args.threshold
            images += 1
            all_required_visible += int(np.all(accepted))
            confidence_gate_reject += int(not np.all(accepted))
            for index, name in enumerate(KEYPOINTS):
                bucket = "visible" if expected[index] else "hidden"
                by_name[name][bucket].append(float(scores[index]))
                if expected[index]:
                    visible_total += 1
                    visible_accept += int(accepted[index])
                else:
                    hidden_total += 1
                    hidden_reject += int(not accepted[index])

    def ratio(value: int, total: int) -> float | None:
        return value / total if total else None

    report = {
        "model": str(args.model), "images": images,
        "threshold": args.threshold,
        "visible_points": visible_total, "hidden_points": hidden_total,
        "visible_recall": ratio(visible_accept, visible_total),
        "hidden_rejection_rate": ratio(hidden_reject, hidden_total),
        "all_eight_accepted_rate": ratio(all_required_visible, images),
        "confidence_gate_reject_rate": ratio(confidence_gate_reject, images),
        "per_keypoint_mean_confidence": {
            name: {
                kind: (float(np.mean(values)) if values else None)
                for kind, values in groups.items()
            } for name, groups in by_name.items()
        },
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
