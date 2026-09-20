"""Private offline acoustic KWS experiment using the actual portable C engine.

Uses the first manually specified phrase as enrollment. Other samples are NOT
training data. Reports distances, never calls ASR or uploads audio.
"""
import argparse
import array
import ctypes as C
import json
from pathlib import Path
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[2]


def library(directory, bounded=False):
    source = ROOT / "app/velafit_ai/algo/velafit_kws_acoustic.c"
    so = str(Path(directory) / "kws.so")
    flags = []
    subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror", *flags,
                    str(source), "-lm", "-o", so], check=True)
    lib = C.CDLL(so)
    if bounded:
        lib.vf_kws_matcher_create = lib.vf_kws_matcher_create_bounded
    lib.vf_kws_frontend_create.restype = C.c_void_p
    lib.vf_kws_frontend_free.argtypes = [C.c_void_p]
    lib.vf_kws_frontend_sample.argtypes = [C.c_void_p, C.c_int16,
                                         C.POINTER(C.c_float), C.POINTER(C.c_float)]
    lib.vf_kws_matcher_create.argtypes = [C.POINTER(C.c_float), C.c_size_t]
    lib.vf_kws_matcher_create.restype = C.c_void_p
    lib.vf_kws_matcher_free.argtypes = [C.c_void_p]
    lib.vf_kws_matcher_feed.argtypes = [C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_uint)]
    lib.vf_kws_matcher_feed.restype = C.c_float
    lib.vf_kws_matcher_feed_gated.argtypes = [C.c_void_p, C.POINTER(C.c_float), C.c_float, C.POINTER(C.c_uint)]
    lib.vf_kws_matcher_feed_gated.restype = C.c_float
    return lib


def read_pcm(path):
    with wave.open(str(path)) as w:
        if (w.getframerate(), w.getnchannels(), w.getsampwidth()) != (44100, 1, 2):
            raise ValueError("Expected44100 mono PCM16")
        if w.getnframes() > 44100 * 60:
            raise ValueError("Recording exceeds diagnostic limit")
        pcm = array.array("h", w.readframes(w.getnframes()))
    import sys
    if sys.byteorder != "little": pcm.byteswap()
    return pcm


def extract(lib, pcm):
    ctx = lib.vf_kws_frontend_create()
    if not ctx: raise MemoryError("frontend")
    out, rms = (C.c_float * 13)(), C.c_float()
    frames = []
    try:
        for index, sample in enumerate(pcm):
            if lib.vf_kws_frontend_sample(ctx, sample, out, C.byref(rms)) == 1:
                frames.append(((index + 1) / 44100, list(out), rms.value))
    finally:
        lib.vf_kws_frontend_free(ctx)
    return frames


def scores(lib, template, frames, gated=False):
    model = (C.c_float * (len(template) * 13))(*[v for row in template for v in row])
    ctx = lib.vf_kws_matcher_create(model, len(template))
    if not ctx: raise ValueError("Invalid model")
    duration = C.c_uint()
    result = []
    try:
        for timestamp, row, rms in frames:
            if gated:
                value = lib.vf_kws_matcher_feed_gated(ctx, (C.c_float * 13)(*row), rms, C.byref(duration))
            else:
                value = lib.vf_kws_matcher_feed(ctx, (C.c_float * 13)(*row), C.byref(duration))
            if value < 1e10:
                result.append({"time": round(timestamp, 3), "distance": round(value, 6),
                               "rms": round(rms, 1), "duration_ms": duration.value * 10})
    finally:
        lib.vf_kws_matcher_free(ctx)
    return result


def private_output(path):
    if not path.resolve().is_relative_to((ROOT / ".secrets").resolve()):
        raise ValueError("Generated voice-derived output must stay in .secrets")
    if path.exists(): raise FileExistsError("Refusing to overwrite private artifact")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--input", type=Path, required=True)
    p.add_argument("--negative", type=Path, required=True)
    p.add_argument("--start", type=float, default=0.55)
    p.add_argument("--end", type=float, default=2.05)
    p.add_argument("--report", type=Path, required=True)
    p.add_argument("--header", type=Path)
    args = p.parse_args()
    private_output(args.report)
    if args.header: private_output(args.header)
    with tempfile.TemporaryDirectory(prefix="velafit-kws-host-") as tmp:
        lib = library(tmp)
        pcm = read_pcm(args.input)
        frames = extract(lib, pcm)
        template = [row for t, row, rms in frames if args.start <= t <= args.end]
        positive = scores(lib, template, frames)
        negative = scores(lib, template, extract(lib, read_pcm(args.negative)))
        heldout = [s for s in positive if s["time"] >= 4.3 and s["rms"] >= 80]
        voice_negative = [s for s in negative if s["rms"] >= 80]
        report = {"method": "MFCC+subsequence-DTW (not neural)",
                  "phrase": "你好，openvela", "template_frames": len(template),
                  "enrollment_window": [args.start, args.end],
                  "same_session_repeat_best": min(heldout, key=lambda x: x["distance"]) if heldout else None,
                  "negative_best": min(voice_negative, key=lambda x: x["distance"]) if voice_negative else None,
                  "accuracy_accepted": False, "network_used": False}
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2))
        args.report.chmod(0o600)
        print(json.dumps(report, ensure_ascii=False))
        if args.header:
            lines = ["/* Private generated voice-derived diagnostic data. DO NOT COMMIT. */",
                     "#define VF_KWS_TEMPLATE_FRAMES %du" % len(template),
                     "static const float g_kws_template[] = {"]
            for row in template: lines.append(",".join(f"{v:.9e}f" for v in row) + ",")
            lines += ["};", "static const int16_t g_kws_test_pcm[] = {"]
            for i in range(0, len(pcm), 16): lines.append(",".join(map(str, pcm[i:i+16])) + ",")
            lines += ["};", "#define VF_KWS_TEST_SAMPLES %du" % len(pcm)]
            args.header.write_text("\n".join(lines) + "\n")
            args.header.chmod(0o600)


if __name__ == "__main__":
    main()
