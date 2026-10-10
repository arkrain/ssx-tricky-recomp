#!/usr/bin/env python3
"""Read US-retail port telemetry without stopping or changing the process.
Linux process_vm_readv requires permission to inspect the selected process.
Use the exact running executable for --binary; symbol addresses change on rebuild.
"""
import argparse
import ctypes
import json
import math
from pathlib import Path
import struct
import subprocess
import time
from sample_clock import sampling_start, wait_until

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('pid', type=int)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--samples', type=int, default=1)
parser.add_argument('--interval', type=float, default=1)
parser.add_argument('--scoring', action='store_true',
                    help='Include human rider multiplier and pickup counters (US retail scoring layout)')
parser.add_argument('--start-at', type=float, help='First sample at this absolute time.monotonic() deadline; setup must finish beforehand')
args = parser.parse_args()
if args.start_at is not None and (not math.isfinite(args.start_at) or args.start_at < 0):
    parser.error('start-at must be a finite nonnegative monotonic time')
if args.samples < 1 or not math.isfinite(args.interval) or args.interval < .01:
    parser.error('samples must be positive and interval at least .01 seconds')
from process_addresses import process_symbols
symbols = process_symbols(args.pid, args.binary, ('g_xbox_mem_offset', 's_render_frame'))

class Iovec(ctypes.Structure):
    _fields_ = [('base', ctypes.c_void_p), ('length', ctypes.c_size_t)]
libc = ctypes.CDLL(None, use_errno=True)
libc.process_vm_readv.argtypes = [ctypes.c_int, ctypes.POINTER(Iovec), ctypes.c_ulong,
                                ctypes.POINTER(Iovec), ctypes.c_ulong, ctypes.c_ulong]
libc.process_vm_readv.restype = ctypes.c_ssize_t

def read(address, size):
    buffer = ctypes.create_string_buffer(size)
    local = Iovec(ctypes.addressof(buffer), size)
    remote = Iovec(address, size)
    if libc.process_vm_readv(args.pid, ctypes.byref(local), 1, ctypes.byref(remote), 1, 0) != size:
        raise OSError(ctypes.get_errno(), f'Cannot read process {args.pid} at {address:#x}')
    return buffer.raw

base = struct.unpack('<Q', read(symbols['g_xbox_mem_offset'], 8))[0]
def u32(address):
    return struct.unpack('<I', read(base+address, 4))[0]
def floats(address):
    return [v if math.isfinite(v) else None for v in struct.unpack('<4f', read(base+address, 16))]

try:
    schedule_start = sampling_start(args.start_at)
except ValueError as error:
    parser.error(str(error))
for sample in range(args.samples):
    wait_until(schedule_start + sample * args.interval)
    result = {'time': time.time(), 'monotonic': time.monotonic(), 'frames': struct.unpack('<I', read(symbols['s_render_frame'], 4))[0]}
    try:
        manager = u32(u32(u32(0x1e3c7c)+0x72c)+0x1c)
        count = u32(manager+0x88)
        if count > 8:
            raise ValueError('Rider manager is not in the expected race state')
        result['riders'] = []
        for i in range(count):
            rider = u32(manager+0xc4+i*4)
            entry = {'base': hex(rider), 'vtable': hex(u32(rider)),
                'is_player': u32(rider)==0x189ed0, 'position': floats(rider+0x170),
                'velocity': floats(rider+0x180), 'rotation': floats(rider+0x1a0),
                'ground_normal': floats(rider+0x2f0)}
            if args.scoring and entry['is_player']:
                # Original opcode14 -> sub_00031A50 -> sub_0005EC70.
                # Read together to reduce sampling skew; the game is not paused.
                scoring = read(base+rider+0x5630, 0xac)
                multiplier = struct.unpack_from('<f', scoring, 0x18)[0]
                entry['scoring'] = {
                    'multiplier': multiplier if math.isfinite(multiplier) else None,
                    'pickup_counts': dict(zip(('x2', 'x3', 'x5'),
                                             struct.unpack_from('<3I', scoring, 0xa0))),
                }
            result['riders'].append(entry)
    except (OSError, ValueError) as error:
        result['rider_error'] = str(error)
    print(json.dumps(result, allow_nan=False), flush=True)
