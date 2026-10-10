#!/usr/bin/env python3
"""Reject performance comparisons whose sampled replay trajectories differ.

Pass two report prefixes produced by measure-gameplay.py and replay controls.
Limits are explicit experimental tolerances, not assertions of pixel identity.
This reads reports only and never changes game state.
"""
import argparse
import json
import math
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('left', type=Path)
p.add_argument('right', type=Path)
p.add_argument('--max-distance', type=float, default=250)
p.add_argument('--max-phase', type=float, default=.05)
a = p.parse_args()
if not math.isfinite(a.max_distance) or a.max_distance < 0 or not math.isfinite(a.max_phase) or a.max_phase < 0:
    p.error('limits must be finite and nonnegative')

def read(prefix):
    states = [json.loads(line) for line in Path(str(prefix)+'-state.jsonl').read_text().splitlines()]
    control = json.loads(Path(str(prefix)+'-control.json').read_text())
    return states, control['play_input_time']

left, lt = read(a.left)
right, rt = read(a.right)
errors = []
# Identical paused scenes are not a valid moving-replay performance comparison.
# Check the entire window so a loop returning near its start is still accepted.
movement = {}
for name, states in [('left', left), ('right', right)]:
    travelled = []
    initial = states[0].get('riders', []) if states else []
    for state in states[1:]:
        for u, v in zip(initial, state.get('riders', [])):
            origin, point = u.get('position', [])[:3], v.get('position', [])[:3]
            if len(origin) == len(point) == 3 and all(n is not None and math.isfinite(n) for n in origin+point):
                travelled.append(math.dist(origin, point))
    movement[name] = max(travelled, default=0)
    if movement[name] <= 10:
        errors.append(f'{name}: no moving replay observed (maximum displacement <=10 game units)')
distances, phases = [], []
if len(left) != len(right) or len(left) < 2:
    errors.append('Sample counts differ or contain fewer than two observations')
for index, (x, y) in enumerate(zip(left, right)):
    phase = abs((x['time']-lt)-(y['time']-rt))
    if not math.isfinite(phase):
        errors.append(f'Sample {index}: invalid timing')
    else:
        phases.append(phase)
    xr, yr = x.get('riders', []), y.get('riders', [])
    if len(xr) != 6 or len(yr) != 6:
        errors.append(f'Sample {index}: expected six riders in both recordings')
        continue
    if sum(bool(r['is_player']) for r in xr) != 1 or sum(bool(r['is_player']) for r in yr) != 1:
        errors.append(f'Sample {index}: expected exactly one human rider')
    for slot, (u, v) in enumerate(zip(xr, yr)):
        if u['is_player'] != v['is_player'] or u['vtable'] != v['vtable']:
            errors.append(f'Sample {index}, slot {slot}: rider roles differ')
        positions = u['position'][:3] + v['position'][:3]
        if len(positions) != 6 or any(n is None or not math.isfinite(n) for n in positions):
            errors.append(f'Sample {index}, slot {slot}: invalid position')
            continue
        distances.append(math.dist(u['position'][:3], v['position'][:3]))
if phases and max(phases) > a.max_phase:
    errors.append('Sample phases exceed tolerance')
if distances and max(distances) > a.max_distance:
    errors.append('Rider trajectories exceed tolerance')
report = {'left': str(a.left), 'right': str(a.right), 'accepted': not errors,
          'limits': {'distance_game_units': a.max_distance, 'phase_seconds': a.max_phase,
                     'minimum_movement_game_units': 10},
          'movement_game_units': movement,
          'max_distance_game_units': max(distances, default=None),
          'max_phase_seconds': max(phases, default=None), 'errors': errors,
          'scope': 'Sampled rider alignment only; does not prove camera, render or audio identity.'}
print(json.dumps(report, indent=2))
raise SystemExit(0 if report['accepted'] else 1)
