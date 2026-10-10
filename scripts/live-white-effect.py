#!/usr/bin/env python3
"""Sample the Tokyo white-effect region without stopping or writing to the game.

Checks a nine-point interior grid in completed 640x480 BGRA frames, not every
pixel. Generation checks reject observed overlapping updates, but cannot make
remote reads atomic. Output is a diagnostic classifier, not a fidelity verdict
or a count of pixel-unique/physically displayed frames. Excludes movie frames.
"""
import argparse
import ctypes as c
import json
import math
from pathlib import Path
import struct
import time

POINTS = tuple((x, y) for y in (32, 240, 440) for x in (48, 320, 592))


def white_grid(pixels):
    return all(all(((pixel >> shift) & 255) >= 254 for shift in (0, 8, 16))
               for pixel in pixels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--poll-ms', type=float, default=1)
    args = parser.parse_args()
    if args.pid <= 0:
        parser.error('pid must be positive')
    if not math.isfinite(args.seconds) or args.seconds <= 0:
        parser.error('seconds must be finite and positive')
    if not math.isfinite(args.poll_ms) or not .1 <= args.poll_ms <= 100:
        parser.error('poll-ms must be finite and between 0.1 and 100')
    from process_addresses import process_symbols
    symbols = process_symbols(args.pid, args.binary,
                              ('fb_completed', 'fb_source_generation', 'movie_active'))

    class Iovec(c.Structure):
        _fields_ = [('base', c.c_void_p), ('size', c.c_size_t)]
    libc = c.CDLL(None, use_errno=True)
    libc.process_vm_readv.argtypes = [c.c_int, c.POINTER(Iovec), c.c_ulong,
                                     c.POINTER(Iovec), c.c_ulong, c.c_ulong]
    libc.process_vm_readv.restype = c.c_ssize_t

    def read(address, size):
        buffer = c.create_string_buffer(size)
        local, remote = Iovec(c.addressof(buffer), size), Iovec(address, size)
        if libc.process_vm_readv(args.pid, c.byref(local), 1, c.byref(remote), 1, 0) != size:
            raise OSError(c.get_errno(), 'Cannot read selected game process')
        return buffer.raw

    def generation():
        return struct.unpack('<Q', read(symbols['fb_source_generation'], 8))[0]

    pixels = (c.c_uint32 * len(POINTS))()
    local = Iovec(c.addressof(pixels), c.sizeof(pixels))
    remote = (Iovec * len(POINTS))(*[
        Iovec(symbols['fb_completed'] + (y * 640 + x) * 4, 4) for x, y in POINTS])
    start = time.monotonic()
    previous = None
    accepted = rejected = coalesced = 0
    print(json.dumps({'event': 'start', 'time': time.time(), 'pid': args.pid,
                      'points': POINTS, 'poll_ms': args.poll_ms,
                      'limitation': 'nine-point classifier; remote reads are not atomic'}), flush=True)
    while time.monotonic() - start < args.seconds:
        begin = time.monotonic()
        seq = generation()
        if seq != previous:
            movie_before = struct.unpack('<i', read(symbols['movie_active'], 4))[0]
            if libc.process_vm_readv(args.pid, c.byref(local), 1, remote, len(POINTS), 0) != c.sizeof(pixels):
                raise OSError(c.get_errno(), 'Cannot read framebuffer grid')
            movie_after = struct.unpack('<i', read(symbols['movie_active'], 4))[0]
            end_seq = generation()
            end = time.monotonic()
            if seq != end_seq or movie_before or movie_after:
                rejected += 1
            else:
                skipped = max(0, seq - previous - 1) if previous is not None else 0
                coalesced += skipped
                print(json.dumps({'elapsed_before': begin-start, 'elapsed_after': end-start,
                                  'generation': seq, 'skipped_generations': skipped,
                                  'white_grid': white_grid(pixels),
                                  'rgb': [int(p) & 0xffffff for p in pixels]}), flush=True)
                accepted += 1
                previous = seq
        time.sleep(args.poll_ms / 1000)
    print(json.dumps({'event': 'complete', 'elapsed': time.monotonic()-start,
                      'accepted': accepted, 'rejected_reads': rejected,
                      'skipped_generations': coalesced}), flush=True)


if __name__ == '__main__':
    main()
