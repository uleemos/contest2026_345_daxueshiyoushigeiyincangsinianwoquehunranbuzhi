#!/usr/bin/env python3
"""Explicitly authorized P4 PCM export. Never print raw audio/serial traffic."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import time
import wave


class PCMReceiver:
    def __init__(self):
        self.size = None
        self.frames = 0
        self.data = bytearray()
        self.done = False
        self.levels = []

    def feed(self, line):
        if self.done:
            raise ValueError("data after end")
        if line.startswith(b"[MIC-LEVEL]"):
            match = re.fullmatch(rb"\[MIC-LEVEL\] end_frame=\d+ channel=(\d+) frames=(\d+) peak=(\d+) rms=([0-9.]+) mean=-?[0-9.]+ nonzero=(\d+) clipped=(\d+)", line)
            if match:
                self.levels.append(dict(channel=int(match[1]), frames=int(match[2]),
                    peak=int(match[3]), rms=float(match[4]), nonzero=int(match[5]),
                    clipped=int(match[6])))
        elif line.startswith(b"[PCM-BEGIN]"):
            match = re.fullmatch(rb"\[PCM-BEGIN\] rate=44100 frames=(\d+) bytes=(\d+)", line)
            if not match or self.size is not None:
                raise ValueError("invalid or duplicate header")
            self.frames, self.size = map(int, match.groups())
            if not 0 < self.frames <= 441000 or self.size != 2 * self.frames:
                raise ValueError("invalid size")
        elif line.startswith(b"[PCM]"):
            match = re.fullmatch(rb"\[PCM\] ([0-9a-f]{8}) ([0-9a-f]+)", line)
            if not match or self.size is None:
                raise ValueError("invalid data frame")
            offset = int(match[1], 16)
            block = bytes.fromhex(match[2].decode("ascii"))
            if offset != len(self.data) or not 0 < len(block) <= 128:
                raise ValueError("invalid sequence or block size")
            self.data.extend(block)
            if len(self.data) > self.size:
                raise ValueError("audio exceeds declared size")
        elif line.startswith(b"[PCM-END]"):
            match = re.fullmatch(rb"\[PCM-END\] fnv32=([0-9a-f]{8})", line)
            if not match or self.size is None or len(self.data) != self.size:
                raise ValueError("incomplete audio")
            value = 2166136261
            for byte in self.data:
                value = ((value ^ byte) * 16777619) & 0xffffffff
            if value != int(match[1], 16):
                raise ValueError("audio checksum mismatch")
            self.done = True

    def wav_bytes(self):
        if not self.done:
            raise ValueError("audio not verified")
        output = io.BytesIO()
        with wave.open(output, "wb") as wav:
            wav.setparams((1, 2, 44100, self.frames, "NONE", "not compressed"))
            wav.writeframes(self.data)
        return output.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--seconds", type=int, choices=range(1, 6), default=5)
    parser.add_argument("--fixture", action="store_true")
    parser.add_argument("--consent-audio-export", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.fixture and not args.consent_audio_export:
        parser.error("human audio export requires --consent-audio-export")
    private = Path(__file__).resolve().parents[2] / ".secrets"
    output = args.output.resolve()
    if not output.is_relative_to(private.resolve()):
        parser.error("output must be inside team .secrets directory")
    if output.exists():
        parser.error("refusing to overwrite existing audio")
    from run_nsh import open_port
    receiver = PCMReceiver()
    command = ("es8311_audio export-voice-fixture" if args.fixture else
               f"es8311_audio check-mic-export {args.seconds}")
    started = time.monotonic()
    with open_port(args.port, started + 15) as port:
        port.reset_input_buffer()
        port.write(b"\n")
        # Synchronize to prompt without exposing terminal output.
        pending = bytearray()
        while time.monotonic() < started + 15:
            pending.extend(port.read(port.in_waiting or 1))
            if b"nsh>" in pending:
                break
        else:
            raise TimeoutError("NSH prompt unavailable")
        port.reset_input_buffer()
        port.write(command.encode("ascii") + b"\n")
        print(json.dumps({"event": "capture_requested", "seconds": args.seconds,
                          "uploaded": False}), flush=True)
        pending.clear()
        capture_finished = False
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline and not receiver.done:
            pending.extend(port.read(port.in_waiting or 1))
            while b"\n" in pending and not receiver.done:
                line, _, rest = pending.partition(b"\n")
                pending = bytearray(rest)
                if line.startswith(b"[MIC-WARMUP]"):
                    print(json.dumps({"event": "capture_warmup_started",
                                      "instruction": "speak_until_capture_finished"}), flush=True)
                if line.startswith(b"[PCM-BEGIN]") and not capture_finished:
                    capture_finished = True
                    print(json.dumps({"event": "capture_finished",
                                      "instruction": "stop_speaking",
                                      "transfer_pending": True}), flush=True)
                receiver.feed(bytes(line).rstrip(b"\r"))
            if len(pending) > 4096:
                raise ValueError("oversized serial line")
        if not receiver.done:
            raise TimeoutError("PCM export incomplete; no WAV written")
    if not args.fixture and not any(receiver.data):
        print(json.dumps({"result": "FAIL", "reason": "all_zero_microphone_pcm",
                          "transfer_verified": True, "levels": receiver.levels,
                          "wav_written": False, "uploaded": False}))
        return 2
    output.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(receiver.wav_bytes())
    print(json.dumps({"result": "VERIFIED_EXPORT", "source": "P4 synthetic fixture" if args.fixture else "P4 microphone",
                      "frames": receiver.frames, "rate": 44100, "channels": 1,
                      "pcm_bytes": len(receiver.data),
                      "levels": receiver.levels,
                      "pcm_sha256": hashlib.sha256(receiver.data).hexdigest(),
                      "elapsed_seconds": round(time.monotonic() - started, 3),
                      "audio_location": "local ignored .secrets", "uploaded": False}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, TimeoutError) as exc:
        # Do not dump serial content or arbitrary exception strings.
        print(json.dumps({"result": "FAIL", "error_type": type(exc).__name__}))
        raise SystemExit(1)
