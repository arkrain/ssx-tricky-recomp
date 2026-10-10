#!/usr/bin/env python3
"""Extract complete SCHl speech streams and independently decode with vgmstream.

Diagnostic only: reads original assets without changing them. Requires numpy,
scipy and a separately built vgmstream-cli; neither is a port dependency.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import wave

import numpy as np
from scipy.signal import resample_poly

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('banks', nargs='+', help='Archive basenames, for example 01a1.dat')
p.add_argument('--archive', type=Path, default=Path('game/data/audio/speech.big'))
p.add_argument('--decoder', type=Path, required=True)
p.add_argument('--output-dir', type=Path, required=True)
a = p.parse_args()
if a.output_dir.exists():
    raise SystemExit('Refusing to overwrite existing reference evidence')
blob = a.archive.read_bytes()
if blob[:4] != b'BIGF':
    raise SystemExit('Expected BIGF archive')
wanted = {name.lower() for name in a.banks}
selected = []
pos = 16
for _ in range(struct.unpack_from('>I', blob, 8)[0]):
    offset, size = struct.unpack_from('>II', blob, pos)
    pos += 8
    end = blob.index(0, pos)
    name = blob[pos:end].decode().replace('\\', '/').split('/')[-1]
    pos = end + 1
    if offset + size > len(blob):
        raise SystemExit('Archive entry exceeds file')
    if name.lower() in wanted:
        selected.append((name, offset, size))
if wanted - {name.lower() for name, _, _ in selected}:
    raise SystemExit('Requested speech bank not found')
a.output_dir.mkdir(parents=True)
manifest = []
for name, offset, size in selected:
    bank = blob[offset:offset + size]
    pos = index = 0
    while (start := bank.find(b'SCHl', pos)) >= 0:
        at = start
        while at + 8 <= len(bank):
            tag = bank[at:at + 4]
            length = struct.unpack_from('<I', bank, at + 4)[0]
            if tag not in (b'SCHl', b'SCCl', b'SCDl', b'SCLl', b'SCEl') or length < 8 or at + length > len(bank):
                raise SystemExit(f'Invalid stream chunk in {name} at {at:#x}')
            at += length
            if tag == b'SCEl':
                break
        else:
            raise SystemExit(f'Unterminated stream in {name}')
        stem = a.output_dir / f'{Path(name).stem}-{index:04d}'
        stream, wav, pcm = (stem.with_suffix(s) for s in ('.asf', '.wav', '.pcm'))
        stream.write_bytes(bank[start:at])
        result = subprocess.run([str(a.decoder.resolve()), '-o', str(wav), str(stream)],
                                check=True, capture_output=True, text=True)
        stem.with_suffix('.txt').write_text(result.stdout + result.stderr)
        with wave.open(str(wav), 'rb') as f:
            channels=f.getnchannels()
            if channels not in (1,2) or f.getsampwidth() != 2:
                raise SystemExit('Reference matcher supports mono/stereo 16-bit speech')
            rate, samples = f.getframerate(), f.getnframes()
            audio = np.frombuffer(f.readframes(samples), dtype='<i2').astype(np.float64).reshape(-1,channels)
        divisor = math.gcd(rate, 48000)
        converted = resample_poly(audio, 48000 // divisor, rate // divisor, axis=0)
        # Preserve independent stereo references; do not silently discard a
        # channel or assume the game's speaker routing is a mono average.
        for channel in range(channels):
            channel_pcm=pcm if channels==1 else stem.with_name(stem.name+f'-ch{channel}').with_suffix('.pcm')
            np.clip(np.rint(converted[:,channel]), -32768, 32767).astype('<i2').tofile(channel_pcm)
            manifest.append(dict(bank=name, index=index, archive_offset=offset + start,
                                 samples=samples, sample_rate=rate, reference_rate=48000,
                                 source_channels=channels, reference_channel=channel,
                                 pcm=str(channel_pcm)))
        pos, index = at, index + 1
(a.output_dir / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'Decoded {len(manifest)} channel references from {len({(x["bank"],x["index"]) for x in manifest})} speech streams')
