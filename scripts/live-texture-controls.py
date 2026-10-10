#!/usr/bin/env python3
"""Sample guest texture registers without stopping or modifying the game.

This observes register state, not every draw or proof of visible pixel usage.
Two matching reads avoid recording a snapshot spanning a concurrent update.
"""
import argparse
import collections
import ctypes
import json
import struct
import subprocess
import time
from pathlib import Path
from process_addresses import process_symbols

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('pid', type=int)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--samples', type=int, default=2000)
p.add_argument('--interval', type=float, default=.01)
a = p.parse_args()
if a.samples < 1 or a.interval < .001:
    p.error('samples must be positive and interval at least .001 seconds')
address = process_symbols(a.pid, a.binary, ('s_tex_reg',))['s_tex_reg']
symbols = subprocess.check_output(['nm', '-S', str(a.binary)], text=True).splitlines()
size = next(int(v[1], 16) for line in symbols
            if len(v := line.split()) == 4 and v[3] == 's_tex_reg')
if size != 256:
    raise SystemExit('Texture register layout changed')

class Iovec(ctypes.Structure):
    _fields_ = [('base', ctypes.c_void_p), ('length', ctypes.c_size_t)]

libc = ctypes.CDLL(None, use_errno=True)
libc.process_vm_readv.argtypes = [ctypes.c_int, ctypes.POINTER(Iovec), ctypes.c_ulong,
                                ctypes.POINTER(Iovec), ctypes.c_ulong, ctypes.c_ulong]
libc.process_vm_readv.restype = ctypes.c_ssize_t

def read():
    data = ctypes.create_string_buffer(size)
    local, remote = Iovec(ctypes.addressof(data), size), Iovec(address, size)
    if libc.process_vm_readv(a.pid, ctypes.byref(local), 1, ctypes.byref(remote), 1, 0) != size:
        raise OSError(ctypes.get_errno(), 'Cannot read texture registers')
    return data.raw

counts = collections.Counter()
unstable = 0
begin = time.monotonic()
for sample in range(a.samples):
    first, second = read(), read()
    if first != second:
        unstable += 1
    else:
        words = struct.unpack('<64I', first)
        for stage in range(4):
            r = words[stage*16:stage*16+16]
            if r[3] & (1 << 30):
                counts[(stage, r[1], r[2], r[3], r[5])] += 1
    if sample + 1 < a.samples:
        time.sleep(a.interval)
rows = []
for (stage, fmt, address_mode, control, filtering), count in counts.most_common():
    rows.append({'stage': stage, 'format': hex(fmt), 'address': hex(address_mode),
                 'control0': hex(control), 'filter': hex(filtering), 'samples': count,
                 'max_anisotropy': 1 << ((control >> 4) & 3),
                 'min_lod': (control >> 18) & 4095, 'max_lod': (control >> 6) & 4095})
print(json.dumps({'samples': a.samples, 'unstable_samples': unstable,
                  'elapsed_seconds': time.monotonic() - begin,
                  'scope': 'Enabled register snapshots; not per-draw or pixel coverage',
                  'states': rows}, indent=2))
