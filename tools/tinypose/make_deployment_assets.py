#!/usr/bin/env python3
"""Package a trained TinyPose model with a real-image INT8 fixture/oracle."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil

import numpy as np
import tensorflow as tf

from keypoint_dataset import read_ppm


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tensor_meta(detail: dict) -> dict:
    scale, zero = detail["quantization"]
    return {"name": detail["name"], "shape": detail["shape"].astype(int).tolist(),
            "dtype": np.dtype(detail["dtype"]).name, "scale": float(scale),
            "zero_point": int(zero)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=pathlib.Path, required=True)
    parser.add_argument("--fixture-image", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output must be a fresh directory")
    args.output.mkdir(parents=True)
    model_path = args.output / "tinypose_int8.tflite"
    shutil.copyfile(args.model, model_path)

    interpreter = tf.lite.Interpreter(
        model_path=str(model_path), num_threads=1,
        experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF)
    interpreter.allocate_tensors()
    inp, out = interpreter.get_input_details()[0], interpreter.get_output_details()[0]
    image = read_ppm(args.fixture_image)
    resized = tf.image.resize(image, (96, 96), method="area").numpy() / 127.5 - 1.0
    scale, zero = inp["quantization"]
    fixture = np.clip(np.rint(resized / scale + zero), -128, 127).astype(np.int8)[None]
    interpreter.set_tensor(inp["index"], fixture)
    interpreter.invoke()
    oracle = interpreter.get_tensor(out["index"]).astype(np.int8)
    fixture_path = args.output / "fixture_int8.bin"
    oracle_path = args.output / "oracle_int8.bin"
    fixture_path.write_bytes(fixture.tobytes())
    oracle_path.write_bytes(oracle.tobytes())
    ops = [entry["op_name"] for entry in interpreter._get_ops_details()
           if entry["op_name"] != "DELEGATE"]
    expected = ["CONV_2D"] * 5 + ["RESHAPE", "FULLY_CONNECTED"]
    if ops != expected:
        raise RuntimeError(f"unexpected operators: {ops}")
    output_scale, output_zero = out["quantization"]
    decoded = (oracle.astype(np.float32) - output_zero) * output_scale
    metadata = {
        "status": "TRAINED_KEYPOINT_MODEL",
        "operators": ops,
        "output_semantic": "8 keypoints x {x, y, confidence}",
        "input_transform": (
            "SC2336 RAW10 -> BGGR demosaic -> portrait_sample CCW90 -> "
            "192x192 letterbox -> area resize 96x96 (no model-input rotation) "
            "-> normalize [-1,1]"
        ),
        "input": tensor_meta(inp), "output": tensor_meta(out),
        "model_bytes": model_path.stat().st_size, "model_sha256": digest(model_path),
        "fixture_source": str(args.fixture_image),
        "fixture_source_sha256": digest(args.fixture_image),
        "fixture_bytes": fixture.nbytes, "fixture_sha256": digest(fixture_path),
        "oracle_bytes": oracle.nbytes, "oracle_sha256": digest(oracle_path),
        "oracle_int8": oracle.reshape(-1).astype(int).tolist(),
        "oracle_float": decoded.reshape(8, 3).astype(float).tolist(),
        "latency_scope": {"included": "one complete TFLM Invoke per measured round",
                          "excluded": ["camera capture", "CCW90 portrait conversion", "resize",
                                       "RGB conversion", "render", "cold model init"]},
    }
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
