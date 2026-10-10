#!/usr/bin/env python3
"""Sample NV2A flip commands without stopping or changing the Linux process.

Flip rate measures guest GPU progress, not unique SDL presentations. Color
clears are included for diagnosis; letterbox bars can cause several per frame.
Use the exact running executable. Avoid builds, debuggers and scene transitions
when collecting performance evidence. Linux process_vm_readv access is needed.
"""
import argparse
import ctypes
import json
from pathlib import Path
import subprocess
import time
from process_addresses import process_symbols

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('pid', type=int)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--samples', type=int, default=7)
parser.add_argument('--interval', type=float, default=5)
args = parser.parse_args()
if args.samples < 1 or args.interval < .01:
    parser.error('samples must be positive and interval at least .01 seconds')
symbols = process_symbols(args.pid, args.binary, ('s_render_frame', 's_gpu'))
sizes = {p[3]: int(p[1], 16) for line in subprocess.check_output(
    ['nm', '-S', str(args.binary)], text=True).splitlines()
    if len(p := line.split()) == 4 and p[3] in symbols}
if sizes.get('s_gpu') != 25352 or sizes.get('s_render_frame') != 4:
    raise SystemExit('GPU state layout changed; update counter offsets before use')

class Iovec(ctypes.Structure):
    _fields_ = [('base', ctypes.c_void_p), ('length', ctypes.c_size_t)]

libc = ctypes.CDLL(None, use_errno=True)
libc.process_vm_readv.argtypes = [ctypes.c_int, ctypes.POINTER(Iovec), ctypes.c_ulong,
                                ctypes.POINTER(Iovec), ctypes.c_ulong, ctypes.c_ulong]
libc.process_vm_readv.restype = ctypes.c_ssize_t

def counter(address):
    value = ctypes.c_uint32()
    local = Iovec(ctypes.addressof(value), 4)
    remote = Iovec(address, 4)
    if libc.process_vm_readv(args.pid, ctypes.byref(local), 1, ctypes.byref(remote), 1, 0) != 4:
        raise OSError(ctypes.get_errno(), f'Cannot read process {args.pid} at {address:#x}')
    return value.value

previous = None
for sample in range(args.samples):
    now = time.monotonic()
    result = {'time': time.time(), 'monotonic': now,
              'clear_frames': counter(symbols['s_render_frame']),
              'flip_commands': counter(symbols['s_gpu'] + 25292)}
    if previous is not None:
        result['flip_rate'] = ((result['flip_commands'] - previous['flip_commands']) & 0xffffffff) / (now - previous['monotonic'])
    print(json.dumps(result), flush=True)
    previous = result
    if sample + 1 < args.samples:
        time.sleep(args.interval)
