#!/usr/bin/env python3
"""Read successful experimental GPU batches and run-time GL failures.

The failure counter does not count normal CPU handling of small, unmatched or
nonfinite batches. Positive successful-batch deltas prove GPU-path use; zero
failures alone do not prove that every draw used the GPU path.
"""
import argparse
import ctypes
import json
import subprocess
import time
from pathlib import Path
from process_addresses import process_symbols

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('pid', type=int)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--samples', type=int, default=7)
p.add_argument('--interval', type=float, default=5)
a = p.parse_args()
if a.samples < 1 or a.interval < .01:
    p.error('samples must be positive and interval at least .01 seconds')
names = ('ssx_gpu_geometry_batches', 'ssx_gpu_geometry_fallbacks')
symbols = process_symbols(a.pid, a.binary, names)
sizes = {parts[3]: int(parts[1], 16) for line in subprocess.check_output(
    ['nm', '-S', str(a.binary)], text=True).splitlines()
    if len(parts := line.split()) == 4 and parts[3] in names}
if any(sizes.get(name) != 8 for name in names):
    raise SystemExit('GPU geometry counter layout changed')

class Iovec(ctypes.Structure):
    _fields_ = [('base', ctypes.c_void_p), ('length', ctypes.c_size_t)]

libc = ctypes.CDLL(None, use_errno=True)
libc.process_vm_readv.argtypes = [ctypes.c_int, ctypes.POINTER(Iovec), ctypes.c_ulong,
                                ctypes.POINTER(Iovec), ctypes.c_ulong, ctypes.c_ulong]
libc.process_vm_readv.restype = ctypes.c_ssize_t

def counter(name):
    value = ctypes.c_uint64()
    local, remote = Iovec(ctypes.addressof(value), 8), Iovec(symbols[name], 8)
    if libc.process_vm_readv(a.pid, ctypes.byref(local), 1, ctypes.byref(remote), 1, 0) != 8:
        raise OSError(ctypes.get_errno(), f'Cannot read {name}')
    return value.value

previous = None
for sample in range(a.samples):
    result = {'time': time.time(), 'monotonic': time.monotonic()}
    result.update({name: counter(name) for name in names})
    if previous:
        elapsed = result['monotonic'] - previous['monotonic']
        result['gpu_batch_rate'] = (result['ssx_gpu_geometry_batches'] - previous['ssx_gpu_geometry_batches']) / elapsed
        result['run_failure_rate'] = (result['ssx_gpu_geometry_fallbacks'] - previous['ssx_gpu_geometry_fallbacks']) / elapsed
    print(json.dumps(result), flush=True)
    previous = result
    if sample + 1 < a.samples:
        time.sleep(a.interval)
