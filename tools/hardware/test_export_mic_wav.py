import io
import unittest
import wave
from export_mic_wav import PCMReceiver


class ExportTests(unittest.TestCase):
    def receiver(self):
        receiver = PCMReceiver()
        receiver.feed(b"[PCM-BEGIN] rate=44100 frames=2 bytes=4")
        return receiver

    def test_valid_wav(self):
        receiver = self.receiver()
        receiver.feed(b"[PCM] 00000000 00000100")
        value = 2166136261
        for byte in b"\x00\x00\x01\x00":
            value = ((value ^ byte) * 16777619) & 0xffffffff
        receiver.feed(f"[PCM-END] fnv32={value:08x}".encode())
        with wave.open(io.BytesIO(receiver.wav_bytes())) as wav:
            self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()), (1, 2, 44100, 2))
            self.assertEqual(wav.readframes(2), b"\x00\x00\x01\x00")

    def test_wrong_offset(self):
        with self.assertRaises(ValueError):
            self.receiver().feed(b"[PCM] 00000002 0100")

    def test_checksum_and_truncation(self):
        for data in (b"[PCM] 00000000 00000100", b"[PCM] 00000000 0000"):
            receiver = self.receiver()
            receiver.feed(data)
            with self.assertRaises(ValueError):
                receiver.feed(b"[PCM-END] fnv32=00000000")

    def test_duplicate_overflow_and_header(self):
        for bad in (b"[PCM-BEGIN] rate=44100 frames=2 bytes=4",
                    b"[PCM] 00000000 0000000000",
                    b"[PCM] 00000000 0"):
            with self.assertRaises(ValueError):
                self.receiver().feed(bad)
        with self.assertRaises(ValueError):
            PCMReceiver().feed(b"[PCM-BEGIN] rate=44100 frames=9999999 bytes=19999998")

    def test_incomplete_cannot_write(self):
        with self.assertRaises(ValueError):
            self.receiver().wav_bytes()


if __name__ == "__main__":
    unittest.main()
