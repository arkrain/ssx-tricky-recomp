#!/usr/bin/env python3
from pathlib import Path
import struct,subprocess,concurrent.futures,json
import numpy as np
import argparse
p=argparse.ArgumentParser(description='Independently demux six EA channels and validate native movie PCM downmix.')
p.add_argument('--intro-pcm',type=Path,required=True);p.add_argument('--eabig-pcm',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
if args.output.exists():raise SystemExit('Refusing to overwrite audit evidence')
root=Path(__file__).resolve().parents[1];folder=root/'build/audio-validation';folder.mkdir(exist_ok=True)
b=(root/'game/data/video/ssxintro.mpc').read_bytes();p=0;chunks=[];header=None
while p+8<=len(b):
 tag=b[p:p+4];size=struct.unpack_from('<I',b,p+4)[0]
 if size<8 or p+size>len(b):raise ValueError('invalid chunk')
 payload=b[p+8:p+size]
 if tag==b'SCHl':header=bytearray(payload);header[15]=1;header[24]=1
 if tag==b'SCDl':chunks.append(payload)
 p+=size
assert header and header[22:25]==b'\x82\x01\x01'
def pack(tag,payload):return tag+struct.pack('<I',len(payload)+8)+payload
def channel(n):
 path=folder/f'movie-channel-{n}.ea';out=path.with_suffix('.pcm')
 with path.open('wb') as f:
  f.write(pack(b'SCHl',header));f.write(pack(b'SCCl',struct.pack('<I',len(chunks))))
  for data in chunks:
   starts=[28+struct.unpack_from('<I',data,4+4*i)[0] for i in range(6)];end=starts[n+1] if n<5 else len(data)
   f.write(pack(b'SCDl',data[:4]+struct.pack('<I',0)+data[starts[n]:end]))
  f.write(pack(b'SCEl',b''))
 subprocess.run(['ffmpeg','-v','error','-y','-i',str(path),'-f','s16le','-acodec','pcm_s16le',str(out)],check=True)
 return np.fromfile(out,dtype='<i2').astype(np.float64)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:channels=list(pool.map(channel,range(6)))
a=np.fromfile(args.intro_pcm,dtype='<i2').reshape(-1,2)
f=2**-.5;normal=1+2*f
expected=np.stack([(channels[0]+f*channels[1]+f*channels[3])/normal,(channels[2]+f*channels[1]+f*channels[4])/normal],axis=1)
error=np.abs(a.astype(float)-np.clip(np.rint(expected),-32768,32767))
subprocess.run(['ffmpeg','-v','error','-y','-i',str(root/'game/data/video/eabig.mpc'),'-map','0:a:0','-f','s16le','-ar','48000','-ac','2',str(folder/'eabig-reference.pcm')],check=True)
eabig=args.eabig_pcm.read_bytes();reference=(folder/'eabig-reference.pcm').read_bytes()
result={'intro_frames':len(a),'intro_seconds':len(a)/48000,'peak':int(np.max(np.abs(a.astype(int)))),'rms':float(np.sqrt(np.mean(a.astype(float)**2))),'downmix_max_sample_error':float(error.max()),'downmix_mean_sample_error':float(error.mean()),'eabig_byte_identical':eabig==reference,'eabig_frames':len(eabig)//4}
args.output.write_text(json.dumps(result,indent=2)+'\n');print(result)
assert result['eabig_byte_identical'] and error.max()<=2
