#!/usr/bin/env python3
"""Identify partial speech using independent references; not a fidelity verdict."""
import argparse,json
from pathlib import Path
import numpy as np
from scipy.signal import correlate,resample_poly
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--capture',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True)
p.add_argument('--output',type=Path,required=True);p.add_argument('--window',type=float,default=.75)
a=p.parse_args()
if not .1<=a.window<=5:p.error('window must be between .1 and5 seconds')
if a.output.exists():raise SystemExit('Refusing to overwrite evidence')
raw=np.fromfile(a.capture,dtype='<i2').reshape(-1,2).astype(float)
signals=[resample_poly(raw[:,c],1,8) for c in range(2)]
sums=[np.r_[0,np.cumsum(s)] for s in signals];squares=[np.r_[0,np.cumsum(s*s)] for s in signals]
length=round(a.window*6000);results=[];variance_cache={}
for item in json.loads(a.manifest.read_text()):
 if 'pcm' not in item:continue
 ref=resample_poly(np.fromfile(item['pcm'],dtype='<i2').astype(float),1,8)
 if len(ref)<600:continue
 windows=[]
 for at in range(0,max(1,len(ref)-length+1),length):
  part=ref[at:at+length].copy();n=len(part)
  if n>len(signals[0]):continue
  part-=part.mean();energy=np.dot(part,part)
  if energy/n<25:continue
  if n not in variance_cache:
   variance_cache[n]=[np.maximum(0,squares[c][n:]-squares[c][:-n]-(sums[c][n:]-sums[c][:-n])**2/n) for c in range(2)]
  best=None
  for c,signal in enumerate(signals):
   variance=variance_cache[n][c]
   score=correlate(signal,part,mode='valid',method='fft')/np.sqrt(np.maximum(variance*energy,1))
   score[variance/n<25]=0;index=int(np.argmax(abs(score)))
   r={'reference_seconds':at/6000,'capture_seconds':index/6000,'channel':c,'correlation':float(score[index]),'duration':n/6000}
   if best is None or abs(r['correlation'])>abs(best['correlation']):best=r
  windows.append(best)
 if windows:
  results.append({'bank':item['bank'],'index':item['index'],'reference_channel':item.get('reference_channel',0),'pcm':item['pcm'],'peak_correlation':max(abs(w['correlation']) for w in windows),'windows':windows})
results.sort(key=lambda r:r['peak_correlation'],reverse=True)
report={'capture':str(a.capture),'manifest':str(a.manifest),'analysis_rate':6000,'window_seconds':a.window,'matches':results}
a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({**report,'matches':results[:5]},indent=2))
