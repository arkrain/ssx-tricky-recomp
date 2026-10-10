#!/usr/bin/env python3
"""Inspect a bounded APU float32 [block][mixbin][32 samples] capture.
Optionally export one bin as dual-mono 48 kHz stereo s16 for the existing
bank-reference matcher. This reads diagnostic copies, never game assets.
"""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('capture',type=Path)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--bin',type=int)
p.add_argument('--pcm',type=Path)
a=p.parse_args()
if a.output.exists():p.error('Output already exists')
if (a.bin is None)!=(a.pcm is None):p.error('--bin and --pcm must be supplied together')
if a.bin is not None and not 0<=a.bin<32:p.error('Bin must be 0..31')
if a.pcm and a.pcm.exists():p.error('PCM output already exists')
raw=np.fromfile(a.capture,dtype='<f4')
if not raw.size or raw.size%(32*32):p.error('Capture is not a whole number of 32-bin blocks')
if not np.isfinite(raw).all():p.error('Capture contains non-finite samples')
bins=raw.reshape(-1,32,32).transpose(1,0,2).reshape(32,-1)
result={'sample_rate':48000,'samples_per_bin':bins.shape[1],'seconds':bins.shape[1]/48000,'bins':[]}
for i,values in enumerate(bins):
 result['bins'].append({'bin':i,'peak':float(np.max(np.abs(values))),'rms':float(np.sqrt(np.mean(values.astype(np.float64)**2))),'nonzero_samples':int(np.count_nonzero(values)),'over_full_scale':int(np.count_nonzero(np.abs(values)>1))})
a.output.write_text(json.dumps(result,indent=2)+'\n')
if a.pcm:
 samples=(np.clip(bins[a.bin],-1,1)*32767).astype('<i2')
 np.repeat(samples[:,None],2,axis=1).tofile(a.pcm)
print(json.dumps(result))
