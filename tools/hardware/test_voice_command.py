"""Host tests for the actual conservative C command classifier."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest


class CommandTest(unittest.TestCase):
    def test_whitelist(self):
        with tempfile.TemporaryDirectory(prefix="velafit-command-test-") as tmp:
            source = Path(__file__).resolve().parents[2] / "app/velafit_ai/algo/velafit_voice_command.c"
            lib = str(Path(tmp) / "command.so")
            subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", lib], check=True)
            parse = ctypes.CDLL(lib).velafit_voice_parse
            parse.argtypes = [ctypes.c_char_p]
            positives = {"你好，openvela": 1, "你好，Open Vela！": 1,
                         "你好，欧鹏薇拉。": 1, "开始训练。": 2,
                         "暂停训练": 3, "继续训练": 4, "结束训练": 5}
            for text, expected in positives.items():
                self.assertEqual(parse(text.encode()), expected, text)
            for text in ("", "你好", "openvela", "不要开始训练", "别结束训练",
                         "开始训练吗？", "开始训练然后结束训练", "你好欧鹏",
                         "欧鹏薇拉", "x" * 256, "电视里说开始训练"):
                self.assertEqual(parse(text.encode()), 0, text)
            self.assertEqual(parse(None), 0)


if __name__ == "__main__":
    unittest.main()
