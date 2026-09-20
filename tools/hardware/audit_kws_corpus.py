#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Audit explicitly selected existing recordings, without recording/uploading.
Output contains only hashes/technical metadata, no raw audio or credentials.
All recordings are development data: NONE is an independent acceptance set.
"""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import sys
import wave

RECORDINGS = {
 'kws-training-03.wav': ('positive','train'),
 'kws-training-04.wav': ('positive','train'),
 'kws-training-05.wav': ('positive','development_holdout'),
 'kws-negative-01.wav': ('negative','train'),
 'kws-negative-02.wav': ('negative','development_holdout'),
}

def inspect(path):
    with wave.open(str(path),'rb') as f:
        channels, width, rate, frames = f.getnchannels(),f.getsampwidth(),f.getframerate(),f.getnframes()
        if channels!=1 or width!=2 or f.getcomptype()!='NONE':
            raise ValueError(f'{path.name}: expected mono PCM16')
        raw=f.readframes(frames)
        if len(raw)!=frames*2 or not frames: raise ValueError('truncated/empty WAV')
    data=array.array('h',raw)
    if sys.byteorder!='little': data.byteswap()
    rms=math.sqrt(sum(int(x)**2 for x in data)/len(data))
    return dict(file=path.name,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                channels=channels,rate=rate,frames=frames,seconds=frames/rate,
                peak=max(abs(x) for x in data),rms=round(rms,3),
                clipped_samples=sum(abs(x)>=32760 for x in data))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--directory',type=Path,
        default=Path(__file__).resolve().parents[2]/'.secrets')
    args=p.parse_args()
    rows=[]; hashes=set()
    for name,(label,split) in RECORDINGS.items():
        row=inspect(args.directory/name)
        if row['sha256'] in hashes: raise ValueError('duplicate original across splits')
        hashes.add(row['sha256'])
        row.update(label=label,split=split,speaker_group='existing-user',
                   independent_acceptance=False)
        rows.append(row)
    print(json.dumps(dict(wake_word='你好，openvela',recordings=rows,
        split_policy='split original recordings BEFORE augmentation; no slice leakage',
        limits=['single known speaker','previously used for tuning',
                'negative recordings too short to establish false accepts/hour',
                'speech boundaries need annotation; full 5s clips include silence']),
        ensure_ascii=False,indent=2))

if __name__=='__main__': main()
