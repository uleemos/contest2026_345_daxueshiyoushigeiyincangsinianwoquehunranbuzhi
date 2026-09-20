"""Local-only comparison, fixed gates; no model deployment or threshold search."""
import argparse
import json
from pathlib import Path
import tempfile

from kws_acoustic_probe import library, read_pcm, extract, scores


def summarize(rows):
    eligible = [r for r in rows if r['rms'] >= 80]
    events = []
    for row in eligible:
        if row['distance'] < 0.080 and (
                not events or row['time'] - events[-1]['time'] >= 2.0):
            events.append(row)
    return {'best': min(eligible, key=lambda r: r['distance']) if eligible else None,
            'candidates': events}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--enrollment', required=True, type=Path)
    parser.add_argument('--start', required=True, type=float)
    parser.add_argument('--end', required=True, type=float)
    parser.add_argument('--positive', required=True, type=Path)
    parser.add_argument('--negative', required=True, type=Path)
    parser.add_argument('--bounded', action='store_true', help='experimental slope-constrained alignment')
    args = parser.parse_args()
    paths = [args.enrollment.resolve(), args.positive.resolve(), args.negative.resolve()]
    if len(set(paths)) != 3:
        parser.error('enrollment and evaluation files must be distinct')
    if not 0 <= args.start < args.end:
        parser.error('invalid enrollment window')
    with tempfile.TemporaryDirectory(prefix='kws-compare-') as directory:
        lib = library(directory, bounded=args.bounded)
        pcm = read_pcm(args.enrollment)
        if args.end > len(pcm) / 44100:
            parser.error('enrollment window exceeds recording')
        template = [row for t, row, rms in extract(lib, pcm)
                    if args.start <= t <= args.end]
        if not 20 <= len(template) <= 250:
            parser.error('template must contain 20..250 frames')
        results = {}
        for label, path in [('positive', args.positive), ('negative', args.negative)]:
            results[label] = summarize(scores(lib, template, extract(lib, read_pcm(path))))
        print(json.dumps({'execution': 'PC portable C engine',
                          'method': 'single-speaker MFCC-DTW prototype',
                          'bounded_experiment': args.bounded,
                          'template_frames': len(template),
                          'window': [args.start, args.end], 'threshold': 0.080,
                          'rms_gate': 80, 'cooldown_seconds': 2,
                          'network_used': False, 'accuracy_accepted': False,
                          'results': results}, ensure_ascii=False))


if __name__ == '__main__':
    main()
