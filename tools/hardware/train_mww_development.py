#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bounded local microWakeWord pipeline experiment, NOT a release wake model.

Reuse original recordings, split before augmentation, board C resampler and
pymicro-features 2.0.2. No uploads, audio playback, or test-set threshold tuning.
Full five-second clips avoid guessing speech annotations; this deliberately
does NOT satisfy the final fast-wakeup latency/accuracy requirement.
Outputs must be private and a fresh directory; no existing model is replaced.
"""
import argparse
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import wave

os.environ.setdefault('TF_NUM_INTRAOP_THREADS', '2')
os.environ.setdefault('TF_NUM_INTEROP_THREADS', '1')
os.environ.setdefault('TF_CPP_MIN_LOG_LEVEL', '2')
os.environ.setdefault('TF_ENABLE_ONEDNN_OPTS', '0')

from audit_kws_corpus import RECORDINGS, inspect

TEAM = Path(__file__).resolve().parents[2]
SOURCE_COMMIT = '4665173cd35f1cff9a61e06fc427f124766c488e'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--steps', type=int, default=40)
    args = p.parse_args()
    private = (TEAM/'.secrets').resolve()
    output = args.output.resolve()
    if not output.is_relative_to(private) or not 1 <= args.steps <= 200:
        p.error('fresh output below .secrets and steps 1..200 required')
    source = private/'micro-wake-word-training'
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != SOURCE_COMMIT:
        raise RuntimeError('training source revision mismatch')
    output.mkdir(parents=True, exist_ok=False)
    (output/'requirements.lock.txt').write_text(subprocess.check_output(
        [sys.executable, '-m', 'pip', 'freeze'], text=True))
    sys.path.insert(0, str(source))
    import numpy as np
    import tensorflow as tf
    from pymicro_features import MicroFrontend
    from microwakeword import mixednet, utils
    from microwakeword.layers import modes
    tf.keras.utils.set_random_seed(345)
    tf.config.experimental.enable_op_determinism()
    rng = np.random.default_rng(345)

    libpath = output/'resample.so'
    resample_source = TEAM/'app/velafit_ai/algo/velafit_resample.c'
    subprocess.run(['gcc', '-std=c11', '-O2', '-shared', '-fPIC',
                    str(resample_source), '-lm', '-o', str(libpath)], check=True)
    lib = c.CDLL(str(libpath))
    lib.vf_resample_create.restype = c.c_void_p
    lib.vf_resample_destroy.argtypes = [c.c_void_p]
    lib.vf_resample_process.argtypes = [c.c_void_p, c.POINTER(c.c_int16), c.c_size_t,
                                      c.POINTER(c.c_int16), c.c_size_t]

    def features(path, gain=1.0):
        with wave.open(str(path), 'rb') as wav:
            if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (1, 2, 44100):
                raise ValueError('expected mono PCM16 44100Hz')
            pcm = np.frombuffer(wav.readframes(wav.getnframes()), dtype='<i2')
        pcm = np.clip(np.rint(pcm.astype(np.float64)*gain), -32768, 32767).astype(np.int16)
        state = lib.vf_resample_create()
        if not state:
            raise MemoryError('resampler')
        converted = []
        try:
            for pos in range(0, len(pcm), 441):
                block = pcm[pos:pos+441]
                target = np.empty(len(block), dtype=np.int16)
                n = lib.vf_resample_process(state, block.ctypes.data_as(c.POINTER(c.c_int16)),
                    len(block), target.ctypes.data_as(c.POINTER(c.c_int16)), len(target))
                if n < 0:
                    raise RuntimeError('C resampler failed')
                converted.append(target[:n].tobytes())
        finally:
            lib.vf_resample_destroy(state)
        frontend = MicroFrontend()
        raw = b''.join(converted)
        rows = []
        pos = 0
        while pos < len(raw):
            result = frontend.process_samples(raw[pos:pos+320])
            if result.samples_read <= 0:
                raise RuntimeError('frontend stalled')
            pos += result.samples_read*2
            if result.features:
                rows.append(list(result.features))
        return np.array(rows, dtype=np.float32)

    manifest = []
    train_x, train_y, holdout = [], [], []
    for name, (label, split) in RECORDINGS.items():
        row = inspect(private/name)
        row.update(label=label, split=split)
        manifest.append(row)
        x = features(private/name)
        if x.shape != (498, 40):
            raise ValueError(f'unexpected feature shape {x.shape}')
        if split == 'train':
            for gain in (0.5, 1.0, 2.0, 4.0):
                train_x.append(features(private/name, gain))
                train_y.append(float(label == 'positive'))
        else:
            holdout.append((name, label, x))
    if len({row['sha256'] for row in manifest}) != len(manifest):
        raise ValueError('duplicate original recording')
    # Synthetic silence only helps exercise the negative path; it is not speech data.
    train_x.extend([np.zeros((498, 40), np.float32)]*4)
    train_y.extend([0.0]*4)
    x = np.array(train_x)
    y = np.array(train_y, dtype=np.float32)[:, None]
    parser = argparse.ArgumentParser()
    mixednet.model_parameters(parser)
    flags = parser.parse_args(['--pointwise_filters', '16,16', '--repeat_in_block', '1,1',
        '--mixconv_kernel_sizes', '[5],[9]', '--residual_connection', '0,0',
        '--first_conv_filters', '16', '--stride', '3'])
    model = mixednet.model(flags, (498, 40), batch_size=1)
    model.compile(optimizer=tf.keras.optimizers.Adam(0.001), loss='binary_crossentropy')
    losses = []
    for step in range(args.steps):
        idx = int(rng.integers(len(x)))
        losses.append(float(model.train_on_batch(x[idx:idx+1], y[idx:idx+1])))
    model.save_weights(output/'development.weights.h5')
    config = dict(stride=3, spectrogram_length=498, train_dir=str(output))
    stream_model = utils.model_to_saved(model, config, modes.Modes.STREAM_INTERNAL_STATE_INFERENCE)
    archive = tf.keras.export.ExportArchive()
    archive.track(stream_model)
    archive.add_endpoint(name='serve', fn=stream_model.call,
        input_signature=[tf.TensorSpec(stream_model.input.shape, tf.float32)])
    archive.write_out(str(output/'saved'))
    converter = tf.lite.TFLiteConverter.from_saved_model(str(output/'saved'))
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter._experimental_variable_quantization = True
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.uint8

    def representative():
        for original in x:
            clip = original.copy()
            clip[0, :2] = [0.0, 26.0]  # Official frontend quantization range anchors.
            for pos in range(0, 498, 3):
                yield [clip[None, pos:pos+3, :]]
    converter.representative_dataset = representative
    model_bytes = converter.convert()
    (output/'development.tflite').write_bytes(model_bytes)
    results = []
    for name, label, clip in holdout:
        # A new interpreter for every recording prevents state leakage.
        interpreter = tf.lite.Interpreter(model_content=model_bytes, num_threads=1)
        interpreter.allocate_tensors()
        inp = interpreter.get_input_details()[0]
        out = interpreter.get_output_details()[0]
        assert tuple(inp['shape']) == (1, 3, 40) and inp['dtype'] == np.int8
        assert out['dtype'] == np.uint8
        scale, zero = inp['quantization']
        outputs = []
        for pos in range(0, 498, 3):
            quantized = np.clip(np.rint(clip[None, pos:pos+3, :]/scale)+zero, -128, 127).astype(np.int8)
            interpreter.set_tensor(inp['index'], quantized)
            interpreter.invoke()
            outputs.append(int(interpreter.get_tensor(out['index']).ravel()[0]))
        results.append(dict(file=name, label=label, final_uint8=outputs[-1],
                            max_uint8=max(outputs), invokes=len(outputs)))
    report = dict(status='DEVELOPMENT_ONLY_NOT_ACCEPTED', source_commit=revision,
        resampler_sha256=hashlib.sha256(resample_source.read_bytes()).hexdigest(),
        recordings=manifest, steps=args.steps, seed=345, last_loss=losses[-1],
        model_bytes=len(model_bytes), model_sha256=hashlib.sha256(model_bytes).hexdigest(),
        frontend='board C 44100->16000; pymicro-features 2.0.2',
        input_shape=[1, 3, 40], input_quantization=list(inp['quantization']),
        development_holdout=results, independent_acceptance=False,
        limits=['five-second context; not fast wakeup acceptance', 'single speaker, tiny corpus',
                'training negative-01 is silence, not confusable speech',
                'no independent negative-hour evaluation', 'not P4 tested'])
    (output/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
