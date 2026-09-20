"""Generate an exactly three-template private bank, no PCM or network output."""
import argparse
from pathlib import Path
import tempfile
from kws_acoustic_probe import library, read_pcm, extract, private_output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--enrollment', nargs=3, action='append', required=True,
                        metavar=('WAV', 'START', 'END'))
    parser.add_argument('--header', type=Path, required=True)
    args = parser.parse_args()
    private_output(args.header)
    if len(args.enrollment) != 3:
        parser.error('exactly three enrollments required')
    models = []
    with tempfile.TemporaryDirectory() as directory:
        lib = library(directory)
        for name, start, end in args.enrollment:
            pcm = read_pcm(Path(name))
            start, end = float(start), float(end)
            if not 0 <= start < end <= len(pcm) / 44100:
                parser.error('invalid enrollment window')
            rows = [row for t, row, rms in extract(lib, pcm)
                    if start <= t <= end and rms >= 80]
            if not 20 <= len(rows) <= 250:
                parser.error('invalid template size')
            models.append(rows)
    lines = ['/* PRIVATE voice-derived data. DO NOT PUBLISH. */']
    for i, rows in enumerate(models):
        lines += [f'static const float g_kws_bank_{i}[] = {{']
        lines += [','.join(f'{v:.9e}f' for v in row) + ',' for row in rows]
        lines += ['};']
    lines += ['static const float * const g_kws_bank[] = {g_kws_bank_0,g_kws_bank_1,g_kws_bank_2};',
              'static const unsigned int g_kws_bank_frames[] = {' +
              ','.join(str(len(rows)) for rows in models) + '};']
    with args.header.open('x') as out:
        out.write('\n'.join(lines) + '\n')
    args.header.chmod(0o600)
    print('Private template frame counts:', [len(rows) for rows in models])


if __name__ == '__main__':
    main()
