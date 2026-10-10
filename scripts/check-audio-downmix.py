#!/usr/bin/env python3
"""Align raw APU mixbins with monitor PCM and compare stereo speaker routing."""
import argparse
import json
from pathlib import Path

import numpy as np
from scipy.signal import correlate, correlation_lags

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bins', type=Path, required=True)
p.add_argument('--pcm', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
if a.output.exists():
    raise SystemExit('Refusing to overwrite existing evidence')
raw = np.fromfile(a.bins, dtype='<f4')
if not raw.size or raw.size % 1024 or not np.isfinite(raw).all():
    raise SystemExit('Invalid raw mixbin capture')
bins = raw.reshape(-1, 32, 32).transpose(1, 0, 2).reshape(32, -1)
pcm = np.fromfile(a.pcm, dtype='<i2').reshape(-1, 2)
level = np.float32(0.7071067811865475)
mixed = np.stack((bins[0] + level * (bins[2] + bins[4]),
                  bins[1] + level * (bins[2] + bins[5])), axis=1)
expected = (np.clip(mixed, -1, 1) * np.float32(32767)).astype('<i2')
legacy = (np.clip(bins[:2].T, -1, 1) * np.float32(32767)).astype('<i2')
channel = int(np.argmax(np.sum(expected.astype(np.float64)**2, axis=0)))
if not np.any(expected):
    raise SystemExit('Silent capture cannot validate routing')
scores = correlate(pcm[:, channel].astype(float), expected[:, channel].astype(float),
                   mode='full', method='fft')
lags = correlation_lags(len(pcm), len(expected), mode='full')
lag = int(lags[np.argmax(scores)])
begin_pcm, begin_bins = max(lag, 0), max(-lag, 0)
length = min(len(pcm) - begin_pcm, len(expected) - begin_bins)
actual = pcm[begin_pcm:begin_pcm + length].astype(np.int32)
def difference(reference):
    delta = actual - reference[begin_bins:begin_bins + length].astype(np.int32)
    return dict(maximum_error=int(np.max(np.abs(delta))),
                rms_error=float(np.sqrt(np.mean(delta.astype(float)**2))),
                exact_samples=int(np.count_nonzero(delta == 0)), total_samples=int(delta.size))
report = dict(bins=str(a.bins), pcm=str(a.pcm), lag_samples=lag,
              overlap_seconds=length / 48000, downmix=difference(expected),
              legacy=difference(legacy),
              clipped_samples=int(np.count_nonzero(np.abs(mixed) > 1)))
a.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
