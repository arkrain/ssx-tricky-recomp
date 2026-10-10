#!/usr/bin/env python3
"""Read SDL presentation and framebuffer-handoff counters without stopping the game.

These count presentation calls and newly consumed handoffs, not monitor
scanouts or pixel-unique frames. Display-work time includes blocking SDL calls.
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
names = ('fb_presentations', 'fb_new_presentations', 'fb_render_ticks',
         'fb_clock_frequency', 'fb_source_generation')
symbols = process_symbols(a.pid, a.binary, names)
sizes = {parts[3]: int(parts[1], 16) for line in subprocess.check_output(
    ['nm', '-S', str(a.binary)], text=True).splitlines()
    if len(parts := line.split()) == 4 and parts[3] in names}
if any(sizes.get(name) != 8 for name in names):
    raise SystemExit('Presentation counter layout changed')

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
    if not result['fb_clock_frequency']:
        raise SystemExit('Presentation thread is not initialized')
    if previous:
        elapsed = result['monotonic'] - previous['monotonic']
        result['presentation_rate'] = (result['fb_presentations'] - previous['fb_presentations']) / elapsed
        result['new_handoff_rate'] = (result['fb_new_presentations'] - previous['fb_new_presentations']) / elapsed
        result['source_handoff_rate'] = (result['fb_source_generation'] - previous['fb_source_generation']) / elapsed
        result['display_work_fraction'] = ((result['fb_render_ticks'] - previous['fb_render_ticks']) /
                                            result['fb_clock_frequency'] / elapsed)
    print(json.dumps(result), flush=True)
    previous = result
    if sample + 1 < a.samples:
        time.sleep(a.interval)
