#!/usr/bin/env python3
"""Verify the firmware camera transform against the frozen INT8 fixture."""

import ctypes
import json
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / ".secrets/tinypose-keypoints-deployment-v1-20260919"
SOURCE = ROOT / "app/velafit_ai/models/velafit_tiny_pose_preprocess.c"


def read_ppm(path):
    data = path.read_bytes()
    header, pixels = data.split(b"\n255\n", 1)
    if not header.startswith(b"P6\n192 192") or len(pixels) != 192 * 192 * 3:
        raise ValueError("expected RGB P6 192x192 fixture source")
    return pixels


def rotate_ccw_rgb(data, side):
    result = bytearray(len(data))
    for y in range(side):
        for x in range(side):
            source = (y * side + x) * 3
            target = ((side - 1 - x) * side + y) * 3
            result[target:target + 3] = data[source:source + 3]
    return bytes(result)


class RuntimePreprocessTest(unittest.TestCase):
    def test_physical_cw90_mount_produces_portrait_model_input(self):
        metadata = json.loads((ASSETS / "metadata.json").read_text())
        upright = read_ppm(ROOT / metadata["fixture_source"])

        # Invert the firmware clockwise transform to reconstruct the RGB192
        # bytes delivered by sc2336_raw10_bggr_letterbox.
        board = bytearray(len(upright))
        for y in range(192):
            for x in range(192):
                source = (y * 192 + x) * 3
                target = ((191 - x) * 192 + y) * 3
                board[target:target + 3] = upright[source:source + 3]

        with tempfile.TemporaryDirectory() as directory:
            library = pathlib.Path(directory) / "preprocess.so"
            subprocess.run([
                "cc", "-shared", "-fPIC", "-O2", "-I", str(SOURCE.parent),
                str(SOURCE), "-lm", "-o", str(library),
            ], check=True)
            module = ctypes.CDLL(str(library))
            function = module.velafit_tiny_pose_preprocess_rgb192
            function.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                 ctypes.c_float, ctypes.c_int]
            input_buffer = (ctypes.c_uint8 * len(board)).from_buffer(board)
            output_buffer = (ctypes.c_int8 * (96 * 96 * 3))()
            function(input_buffer, output_buffer,
                     metadata["input"]["scale"],
                     metadata["input"]["zero_point"])
            actual = bytes(output_buffer)

            debug_function = module.velafit_tiny_pose_debug_rgb96
            debug_function.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
            debug_buffer = (ctypes.c_uint8 * (96 * 96 * 3))()
            debug_function(input_buffer, debug_buffer)
            debug_actual = bytes(debug_buffer)

        # Removing the old runtime CW90 makes the new tensor exactly the old
        # frozen tensor rotated CCW. This is the intentional physical-mount
        # migration; the old trained weights must be retrained before use.
        old_int8 = (ASSETS / "fixture_int8.bin").read_bytes()
        self.assertEqual(actual, rotate_ccw_rgb(old_int8, 96))
        expected = bytearray(96 * 96 * 3)
        for y in range(96):
            for x in range(96):
                for channel in range(3):
                    values = [upright[((2*y+dy)*192+2*x+dx)*3+channel]
                              for dy in range(2) for dx in range(2)]
                    expected[(y*96+x)*3+channel] = (sum(values)+2)//4
        self.assertEqual(debug_actual, rotate_ccw_rgb(bytes(expected), 96))


if __name__ == "__main__":
    unittest.main()
