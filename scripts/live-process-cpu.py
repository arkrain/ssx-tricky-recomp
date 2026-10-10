#!/usr/bin/env python3
"""Read Linux process and initial-thread CPU time without stopping the game.

A rate of 1 means one logical CPU busy for the full interval. This measures
CPU consumption, not game speed or rendered frames. Reject PID reuse.
--threads adds per-thread counters and the last observed CPU, not a migration
trace. Runqueue waiting is reported only when scheduler statistics are enabled.
"""
import argparse
import json
import math
import os
from pathlib import Path
import time
from sample_clock import sampling_start, wait_until

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('pid', type=int)
p.add_argument('--samples', type=int, default=5)
p.add_argument('--interval', type=float, default=5)
p.add_argument('--threads', action='store_true',
               help='Include per-thread CPU, last observed processor and scheduler counters')
p.add_argument('--start-at', type=float, help='First sample at this absolute time.monotonic() deadline; setup must finish beforehand')
a = p.parse_args()
if a.start_at is not None and (not math.isfinite(a.start_at) or a.start_at < 0):
    p.error('start-at must be a finite nonnegative monotonic time')
if a.pid < 1 or a.samples < 2 or not math.isfinite(a.interval) or a.interval < .01:
    p.error('positive PID, at least two samples and interval >= .01 required')
clock = os.sysconf('SC_CLK_TCK')

def counters(path, details=False):
    # comm may contain whitespace or parentheses; subsequent fields cannot.
    stat = path.read_text()
    fields = stat.rsplit(')', 1)[1].split()
    result = {'ticks': int(fields[11]) + int(fields[12]), 'start': int(fields[19])}
    if details:
        result.update(name=stat.split('(', 1)[1].rsplit(')', 1)[0],
                      last_cpu=int(fields[36]), state=fields[0],
                      monotonic=time.monotonic())
    return result


def thread_counters(root):
    result = {}
    for task in sorted((root / 'task').iterdir(), key=lambda p: int(p.name)):
        try:
            row = counters(task / 'stat', details=True)
            try:
                values = list(map(int, (task / 'schedstat').read_text().split()))
                row['schedstat'] = dict(zip(('run_ns', 'wait_ns', 'timeslices'), values[:3]))
            except (FileNotFoundError, PermissionError):
                row['schedstat'] = None
        except (FileNotFoundError, ProcessLookupError):
            # A worker can exit between the directory listing and the read.
            continue
        result[task.name] = row
    return result


def scheduler_stats_enabled():
    try:
        return bool(int(Path('/proc/sys/kernel/sched_schedstats').read_text()))
    except (FileNotFoundError, PermissionError, ValueError):
        return None


root = Path('/proc') / str(a.pid)
previous = None
try:
    schedule_start = sampling_start(a.start_at)
except ValueError as error:
    p.error(str(error))
for i in range(a.samples):
    wait_until(schedule_start + i * a.interval)
    process = counters(root / 'stat')
    thread = counters(root / 'task' / str(a.pid) / 'stat')
    now = {'monotonic': time.monotonic(), 'pid': a.pid,
           'process': process, 'main_thread': thread, 'clock_ticks': clock}
    if a.threads:
        now['threads'] = thread_counters(root)
        now['schedstats_enabled'] = scheduler_stats_enabled()
    if previous:
        elapsed = now['monotonic'] - previous['monotonic']
        for key in ('process', 'main_thread'):
            if now[key]['start'] != previous[key]['start']:
                raise SystemExit('Process identity changed during sampling')
            now[key + '_cpu_rate'] = (now[key]['ticks'] - previous[key]['ticks']) / clock / elapsed
    if previous and a.threads:
        for tid, row in now['threads'].items():
            old = previous['threads'].get(tid)
            if not old or old['start'] != row['start']:
                continue
            elapsed = row['monotonic'] - old['monotonic']
            row['cpu_rate'] = (row['ticks'] - old['ticks']) / clock / elapsed
            current_sched, previous_sched = row['schedstat'], old['schedstat']
            if current_sched and previous_sched and all(
                    current_sched[k] >= previous_sched[k] for k in current_sched):
                row['scheduled_cpu_rate'] = (current_sched['run_ns'] - previous_sched['run_ns']) / 1e9 / elapsed
                # Disabled scheduler statistics can expose a zero wait field;
                # that does not prove that the worker never waited to run.
                if now['schedstats_enabled'] and previous['schedstats_enabled']:
                    row['runqueue_wait_rate'] = (current_sched['wait_ns'] - previous_sched['wait_ns']) / 1e9 / elapsed
                    row['timeslices_delta'] = current_sched['timeslices'] - previous_sched['timeslices']
    print(json.dumps(now), flush=True)
    previous = now
