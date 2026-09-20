"""Generate private gated KWS diagnostic header, never upload voice."""
import argparse
from pathlib import Path
import tempfile
from kws_acoustic_probe import library, read_pcm, extract, private_output


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--enrollment', required=True, type=Path)
    p.add_argument('--start', required=True, type=float)
    p.add_argument('--end', required=True, type=float)
    p.add_argument('--positive', required=True, type=Path)
    p.add_argument('--negative', required=True, type=Path)
    p.add_argument('--header', required=True, type=Path)
    a = p.parse_args()
    private_output(a.header)
    if len({x.resolve() for x in (a.enrollment, a.positive, a.negative)}) != 3:
        p.error('enrollment and test recordings must be distinct')
    pcm = read_pcm(a.enrollment)
    if not 0 <= a.start < a.end <= len(pcm) / 44100:
        p.error('invalid enrollment window')
    with tempfile.TemporaryDirectory() as d:
        rows = [row for t, row, rms in extract(library(d), pcm)
                if a.start <= t <= a.end and rms >= 80]
    if not 20 <= len(rows) <= 250:
        p.error('invalid voiced template length')
    lines = ['/* PRIVATE human voice fixtures: NEVER PUBLISH. */',
             '#define KWS_V3_FRAMES %d' % len(rows),
             'static const float g_kws_v3_template[] = {']
    lines += [','.join(f'{v:.9e}f' for v in row) + ',' for row in rows]
    lines += ['};']
    for name, path in [('positive', a.positive), ('negative', a.negative)]:
        samples = read_pcm(path)
        lines += [f'static const int16_t g_kws_v3_{name}[] = {{']
        lines += [','.join(map(str, samples[i:i+16])) + ',' for i in range(0, len(samples), 16)]
        lines += ['};']
    with a.header.open('x') as stream:
        stream.write('\n'.join(lines) + '\n')
    a.header.chmod(0o600)
    print('Private v3 fixture generated; voiced template frames:', len(rows))


if __name__ == '__main__':
    main()
