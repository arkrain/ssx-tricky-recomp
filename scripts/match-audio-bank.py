#!/usr/bin/env python3
"""Compare captured 48 kHz stereo PCM against independent mono bank decodes."""
import argparse,json
from pathlib import Path
import numpy as np
from scipy.signal import correlate,resample_poly
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--capture',type=Path,required=True)
p.add_argument('--manifest',type=Path,default=Path('build/audio-validation/bank-reference/manifest.json'))
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
if a.output.exists():raise SystemExit('Refusing to overwrite existing match evidence')
raw=np.fromfile(a.capture,dtype='<i2').reshape(-1,2).astype(np.float64)
channels=[resample_poly(raw[:,i],1,8) for i in range(2)]
results=[]
for item in json.loads(a.manifest.read_text()):
    if 'pcm' not in item:continue
    ref=resample_poly(np.fromfile(item['pcm'],dtype='<i2').astype(np.float64),1,8)
    n=len(ref)
    if n<300 or n>len(channels[0]):continue
    ref-=ref.mean();energy=np.dot(ref,ref)
    if energy<1:continue
    best=None
    for channel,signal in enumerate(channels):
        sums=np.concatenate(([0],np.cumsum(signal)));squares=np.concatenate(([0],np.cumsum(signal*signal)))
        variance=np.maximum(0,squares[n:]-squares[:-n]-(sums[n:]-sums[:-n])**2/n)
        dot=correlate(signal,ref,mode='valid',method='fft')
        score=dot/np.sqrt(np.maximum(variance*energy,1))
        score[variance/n<25]=0
        index=int(np.argmax(np.abs(score)))
        result={'bank':item['bank'],'index':item['index'],'reference_channel':item.get('reference_channel',0),'channel':channel,'capture_seconds':index/6000,
            'reference_seconds':n/6000,'correlation':float(score[index])}
        if best is None or abs(result['correlation'])>abs(best['correlation']):best=result
    results.append(best)
results.sort(key=lambda x:abs(x['correlation']),reverse=True)
report={'capture':str(a.capture),'peak':float(np.max(abs(raw))),'rms':float(np.sqrt(np.mean(raw*raw))),'matches':results}
a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({**report,'matches':results[:8]},indent=2))
