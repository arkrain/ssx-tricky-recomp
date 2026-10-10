#!/usr/bin/env python3
"""Compare store semantics and isolated host cost against a runtime snapshot.

Run with games, profilers and builds stopped. This is not a game FPS test.
The shared regression includes device boundaries and consumed/fallback writes;
the timed loop uses volatile RAM addresses and one aperture address per4096.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
p.add_argument('--compiler', default='/usr/bin/gcc')
p.add_argument('--runs', type=int, default=7)
a = p.parse_args()
if a.runs < 1:
    p.error('runs must be positive')
root = Path(__file__).resolve().parents[1]
source = root / 'tests/memory_store.c'
headers = {'baseline': a.baseline.resolve(),
           'candidate': root / 'third_party/xboxrecomp/templates/runtime/recomp_types.h'}
report = {'compiler': subprocess.check_output([a.compiler, '--version'], text=True).splitlines()[0],
          'flags': ['-O2', '-UNDEBUG'], 'writes_per_run': 64000000,
          'test_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
          'interpretation': 'Isolated store loop; not game frame rate.'}
with tempfile.TemporaryDirectory(prefix='ssx-store-') as directory:
    executables = {}
    for name, header in headers.items():
        exe = Path(directory) / name
        subprocess.run([a.compiler, '-O2', '-UNDEBUG',
                        '-DSTORE_HEADER="' + str(header) + '"',
                        str(source), '-o', str(exe), '-lm'], check=True)
        executables[name] = exe
        report[name] = {'header': str(header),
                        'sha256': hashlib.sha256(header.read_bytes()).hexdigest(),
                        'seconds': [], 'hashes': []}
    for run in range(a.runs):
        for name in (('baseline', 'candidate') if run % 2 == 0 else ('candidate', 'baseline')):
            digest, elapsed = subprocess.check_output(
                [str(executables[name]), '--benchmark'], text=True).split()
            report[name]['hashes'].append(digest)
            report[name]['seconds'].append(float(elapsed))
    if len(set(report['baseline']['hashes'] + report['candidate']['hashes'])) != 1:
        raise SystemExit('Store output mismatch')
a.report.parent.mkdir(parents=True, exist_ok=True)
a.report.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
