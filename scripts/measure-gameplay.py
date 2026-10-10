#!/usr/bin/env python3
"""Collect matched Linux CPU, presentation and rider telemetry without pausing.

Use an exact running binary and a controlled input/scene. Counters are not
pixel-unique FPS; this script records evidence, not a performance pass/fail.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics
import signal
import subprocess
import sys

def stop(signum, frame):
    raise KeyboardInterrupt
signal.signal(signal.SIGTERM, stop)

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('pid', type=int)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--report-prefix', type=Path, required=True)
p.add_argument('--samples', type=int, default=13)
p.add_argument('--interval', type=float, default=5)
a = p.parse_args()
if a.pid < 1 or a.samples < 2 or not math.isfinite(a.interval) or a.interval < .01:
    p.error('positive PID, samples >= 2 and finite interval >= .01 required')
outputs = {kind: Path(str(a.report_prefix) + '-' + kind + '.jsonl')
           for kind in ('cpu', 'presentation', 'state')}
summary_path = Path(str(a.report_prefix) + '-summary.json')
if any(f.exists() for f in (*outputs.values(), summary_path)):
    raise SystemExit('Refusing to replace measurement files')
a.report_prefix.parent.mkdir(parents=True, exist_ok=True)
selected = ('RECOMP_NV2A_IDLE_BACKOFF', 'SSX_GL_GPU_GEOMETRY', 'SSX_GL',
            'RECOMP_FB_CAPTURE', 'RECOMP_TEST_INPUT_FILE', 'SSX_SKIP_INTRO')
environment = {}
for raw in Path(f'/proc/{a.pid}/environ').read_bytes().split(b'\0'):
    key, sep, value = raw.partition(b'=')
    if sep and key.decode(errors='replace') in selected:
        environment[key.decode()] = value.decode(errors='replace')
report = {'pid': a.pid, 'binary': str(a.binary.resolve()),
          'binary_sha256': hashlib.sha256(a.binary.read_bytes()).hexdigest(),
          'environment': environment, 'samples': a.samples, 'interval': a.interval}
commands = {'cpu': 'live-process-cpu.py', 'presentation': 'live-presentation-rate.py',
            'state': 'live-game-state.py'}
children = []
handles = []
try:
    for kind, script in commands.items():
        handle = outputs[kind].open('x'); handles.append(handle)
        command = [sys.executable, str(root/'scripts'/script), str(a.pid),
                   '--samples', str(a.samples), '--interval', str(a.interval)]
        if kind != 'cpu': command += ['--binary', str(a.binary.resolve())]
        children.append(subprocess.Popen(command, stdout=handle))
    codes = [child.wait() for child in children]
    if any(codes): raise SystemExit(f'Sampling failed: {codes}')
finally:
    for child in children:
        if child.poll() is None: child.terminate()
    for child in children: child.wait()
    for handle in handles: handle.close()
for kind, path in outputs.items():
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    if len(rows) != a.samples: raise SystemExit('Incomplete '+kind+' sample')
    if kind != 'state':
        metrics = {key: {'mean': statistics.mean(row[key] for row in rows[1:]),
                         'min': min(row[key] for row in rows[1:]),
                         'max': max(row[key] for row in rows[1:])}
                   for key in rows[-1] if key.endswith('_rate')}
        report[kind] = {'elapsed_seconds': rows[-1]['monotonic'] - rows[0]['monotonic'],
                        'metrics': metrics}
    else:
        players = [[r for r in row.get('riders', []) if r.get('is_player')] for row in rows]
        if any(len(riders) != 1 for riders in players):
            raise SystemExit('Expected exactly one human rider throughout the sample')
        players = [riders[0] for riders in players]
        positions = [r['position'][:3] for r in players]
        if any(v is None for point in positions for v in point):
            raise SystemExit('Invalid human rider position')
        report[kind] = {'rider_counts': [len(row['riders']) for row in rows],
                       'first_position': positions[0], 'last_position': positions[-1],
                       'max_displacement_from_first': max(math.dist(positions[0], pos) for pos in positions),
                       'max_speed': max(math.hypot(*r['velocity'][:3]) for r in players),
                       'guest_render_frames': rows[-1]['frames'] - rows[0]['frames']}
summary_path.write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
