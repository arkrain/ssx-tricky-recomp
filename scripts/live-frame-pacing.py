#!/usr/bin/env python3
"""Observe framebuffer handoff intervals without stopping the Linux game.

Counter transitions are bracketed by read times, not exact presentation times.
These are source/SDL handoffs, not pixel-unique frames or monitor scanouts.
Coalesced transitions are retained but excluded from interval estimates.
"""
import argparse
import ctypes as c
import json
import math
from pathlib import Path
import subprocess
import time
from sample_clock import sampling_start, wait_until
from process_addresses import process_symbols

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('pid', type=int)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--seconds', type=float, default=60)
p.add_argument('--poll-ms', type=float, default=1)
p.add_argument('--start-at', type=float, help='First sample at this absolute time.monotonic() deadline; setup must finish beforehand')
a = p.parse_args()
if a.start_at is not None and (not math.isfinite(a.start_at) or a.start_at < 0):
    p.error('start-at must be a finite nonnegative monotonic time')
if a.pid < 1 or not math.isfinite(a.seconds) or not 0 < a.seconds <= 600:
    p.error('positive PID and duration up to600 seconds required')
if not math.isfinite(a.poll_ms) or not .25 <= a.poll_ms <= 20:
    p.error('poll interval must be0.25..20ms')
names = ('fb_source_generation', 'fb_new_presentations', 'fb_presentations')
symbols = process_symbols(a.pid, a.binary, names)
sizes = {v[3]: int(v[1], 16) for line in subprocess.check_output(
    ['nm', '-S', str(a.binary)], text=True).splitlines()
    if len(v := line.split()) == 4 and v[3] in names}
if any(sizes.get(n) != 8 for n in names):
    raise SystemExit('Presentation counter layout changed')

class Iovec(c.Structure):
    _fields_ = [('base', c.c_void_p), ('size', c.c_size_t)]

lib = c.CDLL(None, use_errno=True)
lib.process_vm_readv.argtypes = [c.c_int, c.POINTER(Iovec), c.c_ulong,
                               c.POINTER(Iovec), c.c_ulong, c.c_ulong]
lib.process_vm_readv.restype = c.c_ssize_t
values = (c.c_uint64 * len(names))()
local = Iovec(c.addressof(values), c.sizeof(values))
remote = (Iovec * len(names))(*(Iovec(symbols[n], 8) for n in names))

def read():
    lo = time.monotonic()
    if lib.process_vm_readv(a.pid, c.byref(local), 1, remote, len(names), 0) != c.sizeof(values):
        raise OSError(c.get_errno(), 'Cannot read presentation counters')
    return lo, time.monotonic(), list(values)

def emit(row):
    print(json.dumps(row), flush=True)

try:
    sampling_start(a.start_at)
except ValueError as error:
    p.error(str(error))
start, previous_hi, previous = read()
previous_lo = start
initial = previous[:]
last_event = [None] * len(names)
last_change = [(start, previous_hi)] * len(names)
intervals = [[] for _ in names]
coalesced = [0] * len(names)
polls = 0
max_poll_gap = 0
emit({'event': 'start', 'pid': a.pid, 'binary': str(a.binary.resolve()),
      'monotonic': start, 'names': names, 'values': previous,
      'poll_ms': a.poll_ms, 'seconds': a.seconds})
while previous_hi - start < a.seconds:
    time.sleep(a.poll_ms / 1000)
    lo, hi, current = read()
    polls += 1
    max_poll_gap = max(max_poll_gap, hi - previous_lo)
    for i, name in enumerate(names):
        delta = current[i] - previous[i]
        if delta < 0:
            raise RuntimeError('Counter reset during observation')
        if not delta:
            continue
        row = {'event': 'transition', 'counter': name, 'value': current[i],
               'delta': delta, 'lo': previous_lo - start, 'hi': hi - start}
        old = last_event[i]
        if delta == 1 and old is not None:
            lower = max(0, previous_lo - old[1]) * 1000
            upper = (hi - old[0]) * 1000
            row.update(interval_min_ms=lower, interval_max_ms=upper)
            intervals[i].append((lower, upper))
        if delta > 1:
            coalesced[i] += delta
        # A coalesced batch cannot supply a single preceding event time.
        last_event[i] = (previous_lo, hi) if delta == 1 else None
        last_change[i] = (previous_lo, hi)
        emit(row)
    previous_lo, previous_hi, previous = lo, hi, current

def percentile(seq, fraction):
    return sorted(seq)[max(0, math.ceil(len(seq) * fraction) - 1)] if seq else None

summary = {}
for i, name in enumerate(names):
    pairs = intervals[i]
    summary[name] = {'count': previous[i] - initial[i],
                     'rate': (previous[i] - initial[i]) / (previous_hi - start),
                     'bounded_intervals': len(pairs), 'coalesced_events': coalesced[i],
                     'trailing_silence_bounds_ms': [
                         max(0, previous_lo - last_change[i][1]) * 1000,
                         (previous_hi - last_change[i][0]) * 1000]}
    for label, fraction in [('p50', .5), ('p95', .95), ('p99', .99), ('max', 1)]:
        summary[name][label + '_interval_bounds_ms'] = [
            percentile([v[j] for v in pairs], fraction) for j in (0, 1)]
emit({'event': 'summary', 'elapsed': previous_hi - start, 'polls': polls,
      'max_poll_window_ms': max_poll_gap * 1000, 'counters': summary})
