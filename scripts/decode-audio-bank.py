#!/usr/bin/env python3
"""Decode this title's mono EA-XA bank samples for independent audio comparison.

Reads the user's audio.big, wraps verified BNKl/PT07 samples in an EA stream,
and delegates ADPCM decoding/resampling to FFmpeg. Original assets are unchanged.
Format reference: https://github.com/vgmstream/vgmstream/blob/master/src/meta/ea_schl.c
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('banks',nargs='+',help='Bank basenames, for example C_Eddi01.bnk')
p.add_argument('--output-dir',type=Path,default=Path('build/audio-validation/bank-reference'))
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
b=(root/'game/data/audio/audio.big').read_bytes()
assert b[:4]==b'BIGF'
wanted={name.lower() for name in a.banks};banks={};pos=16
for _ in range(struct.unpack_from('>I',b,8)[0]):
    offset,size=struct.unpack_from('>II',b,pos);pos+=8
    end=b.index(0,pos);name=b[pos:end].decode().replace('\\','/').split('/')[-1];pos=end+1
    assert offset+size<=len(b)
    if name.lower() in wanted:banks[name]=b[offset:offset+size]
if wanted-{name.lower() for name in banks}:raise SystemExit('Requested bank not found')
a.output_dir.mkdir(parents=True,exist_ok=True)
def chunk(tag,data):return tag+struct.pack('<I',len(data)+8)+data
def tag(number,value):
    data=value.to_bytes(max(1,(value.bit_length()+7)//8),'big')
    return bytes([number,len(data)])+data
report=[]
for name,bank in banks.items():
    assert bank[:6]==b'BNKl\x05\0'
    for index in range(struct.unpack_from('<H',bank,6)[0]):
        entry=0x14+index*4;relative=struct.unpack_from('<I',bank,entry)[0]
        if not relative:continue
        pos=entry+relative
        assert bank[pos:pos+4]==b'PT\x07\0';pos+=4
        tags={}
        while pos<len(bank):
            number=bank[pos];pos+=1
            if number==0xff:break
            if number in (0xfc,0xfd):continue
            size=bank[pos];pos+=1
            assert pos+size<=len(bank)
            tags[number]=int.from_bytes(bank[pos:pos+size],'big');pos+=size
        channels=tags.get(0x82,1);rate=tags.get(0x84,24000)
        if channels!=1 or tags.get(0xa0,8)!=10 or tags.get(0x80,2)!=2:
            report.append({'bank':name,'index':index,'skipped':'not verified mono Xbox EA-XA'});continue
        samples=tags[0x85];offset=tags[0x88];blocks=(samples+27)//28
        assert 0<samples<rate*600 and 0<offset<=offset+blocks*15<=len(bank)
        header=b'PT\x07\0\xfd'+tag(0x82,1)+tag(0x84,rate)+tag(0x85,samples)+tag(0xa0,10)+b'\xff'
        packet=struct.pack('<III',blocks*28,0,0)+bank[offset:offset+blocks*15]
        # FFmpeg's EA header scan expects at least five chunks, even audio-only.
        data=chunk(b'SCHl',header.ljust(40,b'\0'))+chunk(b'SCCl',struct.pack('<I',1))*2+chunk(b'SCDl',packet)+chunk(b'SCEl',b'')
        stem=a.output_dir/f'{Path(name).stem}-{index:03d}'
        stream=stem.with_suffix('.ea');pcm=stem.with_suffix('.pcm');stream.write_bytes(data)
        subprocess.run(['ffmpeg','-v','error','-y','-i',str(stream),'-af',f'atrim=end_sample={samples}',
            '-ar','48000','-ac','1','-f','s16le','-acodec','pcm_s16le',str(pcm)],check=True)
        report.append({'bank':name,'index':index,'samples':samples,'sample_rate':rate,'reference_rate':48000,'pcm':str(pcm)})
(a.output_dir/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
print(f'Decoded {sum("pcm" in item for item in report)} samples; skipped {sum("skipped" in item for item in report)}')
