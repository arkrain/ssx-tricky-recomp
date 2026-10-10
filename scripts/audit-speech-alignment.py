#!/usr/bin/env python3
"""Refine an identified speech match at48kHz; report local timing, not a pass gate."""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--matches',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True)
p.add_argument('--output',type=Path,required=True);p.add_argument('--bank');p.add_argument('--index',type=int)
a=p.parse_args()
if a.output.exists():raise SystemExit('Refusing to overwrite alignment evidence')
match=json.loads(a.matches.read_text());manifest=json.loads(a.manifest.read_text())
if not match['matches']:raise SystemExit('No identified match')
bank=a.bank or match['matches'][0]['bank'];index=a.index if a.index is not None else match['matches'][0]['index']
raw=np.fromfile(match['capture'],dtype='<i2').reshape(-1,2).astype(float);results=[]
def normalized(x,y):
 x=x-x.mean();y=y-y.mean();energy=np.dot(x,x);other=np.dot(y,y)
 return (float(np.dot(x,y)/np.sqrt(energy*other)),float(np.dot(x,y)/energy)) if energy>0 and other>0 else (0.,0.)
for item in manifest:
 if item['bank']!=bank or item['index']!=index:continue
 ref_channel=item.get('reference_channel',0)
 rows=[r for r in match['matches'] if r['bank']==bank and r['index']==index and r.get('reference_channel',0)==ref_channel]
 if len(rows)!=1:raise SystemExit('Ambiguous/missing channel match')
 row=rows[0];channel=row['channel'];ref=np.fromfile(item['pcm'],dtype='<i2').astype(float)
 center=round(row['capture_seconds']*48000);candidates=[]
 for lag in range(max(0,center-16),min(len(raw)-len(ref),center+16)+1):
  corr,gain=normalized(ref,raw[lag:lag+len(ref),channel]);candidates.append((corr,lag,gain))
 if not candidates:raise SystemExit('Reference does not fit capture')
 corr,lag,gain=max(candidates)
 result={'reference_channel':ref_channel,'channel':channel,'lag_samples':lag,'correlation':corr,'gain':gain,'reference_samples':len(ref),'window_lags':[]}
 for at in range(0,len(ref)-36000+1,36000):
  x=ref[at:at+36000];energy=np.var(x)
  if energy<25:continue
  local=[]
  for offset in range(max(0,lag-12),min(len(raw)-at-len(x),lag+12)+1):
   score,_=normalized(x,raw[offset+at:offset+at+len(x),channel]);local.append((score,offset))
  score,offset=max(local);result['window_lags'].append({'reference_seconds':at/48000,'lag_samples':offset,'correlation':score})
 results.append(result)
r={'capture':match['capture'],'bank':bank,'index':index,'analysis_rate':48000,'coarse_search_radius_samples':16,'window_search_radius_samples':12,'channels':results,'peak':int(np.max(abs(raw))),'clipped_samples':int(np.sum((raw<=-32768)|(raw>=32767)))}
a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
