#!/usr/bin/env python3
"""Read presentation state during a transition without stopping the game.

Reports sampled RGB activity in the completed frame and current guest surface.
The latter may be mid-draw; it is diagnostic evidence, not a finished frame.
"""
import argparse
import ctypes
import json
import struct
import time
from process_addresses import process_symbols

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('pid', type=int)
p.add_argument('--binary', required=True)
p.add_argument('--seconds', type=float, default=120)
p.add_argument('--interval', type=float, default=.5)
a = p.parse_args()
if a.seconds <= 0 or a.interval < .05:
    p.error('seconds must be positive and interval at least .05')
symbols = process_symbols(a.pid, a.binary, (
    'g_xbox_mem_offset', 'fb_address', 'fb_stride', 'fb_completed', 'movie_active'))

class Iov(ctypes.Structure):
    _fields_ = [('base', ctypes.c_void_p), ('size', ctypes.c_size_t)]

lib = ctypes.CDLL(None, use_errno=True)
lib.process_vm_readv.restype = ctypes.c_ssize_t
lib.process_vm_readv.argtypes = [ctypes.c_int, ctypes.POINTER(Iov), ctypes.c_ulong,
                               ctypes.POINTER(Iov), ctypes.c_ulong, ctypes.c_ulong]

def read(address, size):
    buffer = ctypes.create_string_buffer(size)
    local, remote = Iov(ctypes.addressof(buffer), size), Iov(address, size)
    if lib.process_vm_readv(a.pid, ctypes.byref(local), 1, ctypes.byref(remote), 1, 0) != size:
        raise OSError(ctypes.get_errno(), 'process_vm_readv failed')
    return buffer.raw

def u32(name):
    return struct.unpack('<I', read(symbols[name], 4))[0]

def activity(pixels):
    # A regular 80x60 grid, excluding alpha, sufficient to distinguish a
    # completely black frame from course geometry and the menu overlays.
    return sum(any(pixels[(y*640+x)*4:(y*640+x)*4+3])
               for y in range(4,480,8) for x in range(4,640,8))

base = struct.unpack('<Q', read(symbols['g_xbox_mem_offset'], 8))[0]
end = time.monotonic() + a.seconds
while time.monotonic() < end:
    address, pitch = u32('fb_address'), u32('fb_stride')
    row = {'time': time.time(), 'address': hex(address), 'pitch': pitch,
           'movie': u32('movie_active'),
           'completed_lit_samples': activity(read(symbols['fb_completed'], 640*480*4))}
    if pitch == 2560 and ((0x1000 <= address <= 0x04000000-640*480*4) or
                          (0x80000000 <= address <= 0x84000000-640*480*4)):
        row['guest_lit_samples'] = activity(read(base+address, 640*480*4))
    print(json.dumps(row), flush=True)
    time.sleep(a.interval)
