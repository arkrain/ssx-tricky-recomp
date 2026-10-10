#!/usr/bin/env python3
"""Compare float-attribute decoding and isolated timing against a source snapshot.

Run timing with the game closed. This is a synthetic attribute-fetch probe,
not an in-game frame-rate measurement. Existing raster assertions also run.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--runs', type=int, default=7)
p.add_argument('--report', type=Path, required=True)
a = p.parse_args()
if a.runs < 1:
    p.error('runs must be positive')
root = Path(__file__).resolve().parents[1]
build = root / 'build/linux-release'
commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'raster_test'], text=True).splitlines()
compile = next(shlex.split(x) for x in commands if ' -c ' in x and '/tests/raster.c' in x)
link = [x for x in shlex.split(commands[-1])[2:-2] if not x.startswith('-Wl,--dependency-file=')]
probe = r'''
    uint32_t rng=23;uint64_t hash=1469598103934665603ULL;
    for(unsigned i=0;i<4096;i++) {
        rng=rng*1664525u+1013904223u;
        memcpy(ram+0x20000+i*4,&rng,4);
    }
    struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
    for(unsigned n=0;n<16000000;n++) {
        /* Mostly float4, with shorter and zero-stride arrays mixed in. */
        VertexAttr at={0x20000+(n&3),2,n%8<5?4:1+n%3,n%13?16:0};
        float v[4];uint32_t bits[4];
        if(!fetch_attr(&at,(n*17)&511,v))abort();
        memcpy(bits,v,16);
        for(unsigned k=0;k<4;k++)hash=(hash^bits[k])*1099511628211ULL;
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    printf("%016llx %.9f\n",(unsigned long long)hash,
        end.tv_sec-begin.tv_sec+(end.tv_nsec-begin.tv_nsec)*1e-9);
'''
report = {}
with tempfile.TemporaryDirectory(prefix='ssx-fetch-') as work:
    folder = Path(work)
    executables = {}
    for name, source in [('old', a.baseline.resolve()), ('new', root / 'third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c')]:
        c = folder / (name + '.c')
        obj, exe = c.with_suffix('.o'), c.with_suffix('')
        text = (root / 'tests/raster.c').read_text().replace(
            '#include "../third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c"',
            '#include <time.h>\n#include "' + str(source) + '"').replace(
            '    free(ram); return 0;', probe + '\n    free(ram); return 0;')
        c.write_text(text)
        cmd = compile.copy()
        cmd[cmd.index('-o') + 1], cmd[cmd.index('-c') + 1] = str(obj), str(c)
        subprocess.run(cmd, cwd=build, check=True, stdout=subprocess.DEVNULL)
        cmd = link.copy()
        cmd[cmd.index('CMakeFiles/raster_test.dir/tests/raster.c.o')] = str(obj)
        cmd[cmd.index('-o') + 1] = str(exe)
        subprocess.run(cmd, cwd=build, check=True)
        executables[name] = exe
        report[name] = {'source': str(source), 'hash': None, 'elapsed_seconds': []}
    # Alternate execution order to reduce systematic thermal/order bias.
    for run in range(a.runs):
        for name in (('old', 'new') if run % 2 == 0 else ('new', 'old')):
            result = subprocess.run([str(executables[name])], capture_output=True, text=True)
            if result.returncode:
                print(result.stdout, result.stderr)
                result.check_returncode()
            digest, elapsed = result.stdout.split()
            if report[name]['hash'] not in (None, digest):
                raise SystemExit('Nondeterministic attribute output')
            report[name]['hash'] = digest
            report[name]['elapsed_seconds'].append(float(elapsed))
    if report['old']['hash'] != report['new']['hash']:
        raise SystemExit('Attribute output differs')
a.report.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
