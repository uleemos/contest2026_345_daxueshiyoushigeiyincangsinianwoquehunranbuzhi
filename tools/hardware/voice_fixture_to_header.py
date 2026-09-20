"""Build a local-only, amplitude-limited PCM fixture from a SAPI WAV."""
import argparse
from collections import deque
import hashlib
import math
import pathlib
import struct
import wave


def compress_speech(samples, ceiling=1875, max_gain=8.0, radius=441):
    """Look-ahead peak envelope; gain release is 50 ms, no hard clipping.

    Samples are already peak-normalized. Include the current sample in
    the envelope so gain never permits a sample above the original ceiling.
    This is a diagnostic fixture transform, not a production loudness meter.
    """
    peaks = deque()
    right = 0
    gain = 1.0
    result = []
    release = 1.0 - math.exp(-1.0 / (44100 * 0.050))
    for index, sample in enumerate(samples):
        end = min(len(samples), index + radius + 1)
        while right < end:
            value = abs(samples[right])
            while peaks and peaks[-1][1] <= value:
                peaks.pop()
            peaks.append((right, value))
            right += 1
        while peaks and peaks[0][0] < index - radius:
            peaks.popleft()
        target = min(max_gain, ceiling / max(1, peaks[0][1]))
        gain = min(target, gain + release * (target - gain))
        result.append(round(sample * gain))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('wav', type=pathlib.Path)
    parser.add_argument('header', type=pathlib.Path)
    parser.add_argument('--compress', action='store_true',
                        help='Lift quiet speech while preserving the peak ceiling')
    args = parser.parse_args()
    with wave.open(str(args.wav), 'rb') as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate(),
                source.getcomptype()) != (1, 2, 44100, 'NONE'):
            raise ValueError('Expected mono PCM16, 44100 Hz')
        count = source.getnframes()
        if not 0 < count <= 10 * 44100:
            raise ValueError('Fixture must last at most 10 seconds')
        raw = source.readframes(count)
    samples = struct.unpack('<' + 'h' * count, raw)
    peak = max(abs(sample) for sample in samples)
    if peak == 0:
        raise ValueError('Silent fixture')
    limited = [round(sample * 1875 / peak) for sample in samples]
    before_rms = math.sqrt(sum(sample * sample for sample in limited) / count)
    if args.compress:
        limited = compress_speech(limited)
    after_rms = math.sqrt(sum(sample * sample for sample in limited) / count)
    assert max(map(abs, limited)) <= 1875
    digest = hashlib.sha256(raw).hexdigest()
    header = ['/* Generated local synthetic speech; do not redistribute. */',
              '#define VOICE_FIXTURE_AVAILABLE 1',
              f'#define VOICE_FIXTURE_FRAMES {count}u',
              'static const int16_t g_voice_pcm[] = {']
    for offset in range(0, count, 16):
        header.append('  ' + ', '.join(map(str, limited[offset:offset + 16])) + ',')
    header.append('};\n')
    args.header.write_text('\n'.join(header), encoding='ascii')
    print(f'frames={count} seconds={count / 44100:.3f} input_peak={peak} '
          f'output_peak={max(map(abs, limited))} rms_before={before_rms:.1f} '
          f'rms_after={after_rms:.1f} compressed={args.compress} '
          f'source_pcm_sha256={digest}')


if __name__ == '__main__':
    main()
