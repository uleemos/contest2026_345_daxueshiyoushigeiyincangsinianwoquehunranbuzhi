"""Exercise the actual portable device WAV/JSON builder with synthetic PCM."""
import base64
import ctypes
import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import wave


class BuilderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="velafit-asr-test-")
        source = Path(__file__).resolve().parents[2] / "app/c6_wifi/c6_asr_body.c"
        library = str(Path(cls.tmp.name) / "builder.so")
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        str(source), "-o", library], check=True)
        cls.lib = ctypes.CDLL(library)
        cls.lib.c6_asr_body.argtypes = [ctypes.POINTER(ctypes.c_int16),
            ctypes.c_size_t, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p),
            ctypes.POINTER(ctypes.c_size_t)]
        cls.libc = ctypes.CDLL(None)
        cls.libc.free.argtypes = [ctypes.c_void_p]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def build(self, values, rate):
        pcm = (ctypes.c_int16 * len(values))(*values)
        pointer, length = ctypes.c_void_p(), ctypes.c_size_t()
        ret = self.lib.c6_asr_body(pcm, len(values), rate,
                                 ctypes.byref(pointer), ctypes.byref(length))
        try:
            return ret, ctypes.string_at(pointer, length.value) if pointer.value else None
        finally:
            self.libc.free(pointer)

    def test_roundtrip(self):
        for rate in (16000, 44100):
            for count in (1, 2, 3, 4, 5, 16000):
                values = ([-32768, -1, 0, 1, 32767] * (count // 5 + 1))[:count]
                ret, data = self.build(values, rate)
                self.assertEqual(ret, 0)
                body = json.loads(data)
                self.assertEqual(body["model"], "mimo-v2.5-asr")
                self.assertEqual(body["asr_options"], {"language": "auto"})
                audio = body["messages"][0]["content"][0]["input_audio"]["data"]
                prefix, encoded = audio.split(",", 1)
                self.assertEqual(prefix, "data:audio/wav;base64")
                with wave.open(io.BytesIO(base64.b64decode(encoded, validate=True))) as wav:
                    self.assertEqual((wav.getnchannels(), wav.getsampwidth(),
                                      wav.getframerate(), wav.getnframes()),
                                     (1, 2, rate, count))
                    self.assertEqual(wav.readframes(count), struct.pack("<" + "h" * count, *values))

    def test_limits(self):
        for values, rate in (([], 16000), ([0], 0), ([0], 48000),
                             ([0] * 160001, 16000)):
            ret, data = self.build(values, rate)
            self.assertLess(ret, 0)
            self.assertIsNone(data)


if __name__ == "__main__":
    unittest.main()
