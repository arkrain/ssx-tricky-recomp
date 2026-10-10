#!/usr/bin/env python3
"""Compare full shader input banks and isolated assembly cost with a snapshot.

Run with the game closed. Mixed masks/formats and changing broadcast/fallback
data exercise the input contract; timings are a microbenchmark, not FPS.
The current raster assertions also run against both implementations.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
p.add_argument('--runs', type=int, default=7)
a = p.parse_args()
if a.runs < 1:
    p.error('runs must be positive')
root = Path(__file__).resolve().parents[1]
build = root / 'build/linux-release'
commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'raster_test'], text=True).splitlines()
compile = next(shlex.split(x) for x in commands if ' -c ' in x and '/tests/raster.c' in x)
link = [x for x in shlex.split(commands[-1])[2:-2] if not x.startswith('-Wl,--dependency-file=')]
old_assembly = '''float input[16][4]={{0}};
for(unsigned i=0;i<16;i++)if(s_vertex_inputs&(1u<<i))fetch_vertex_input(i,v,input[i]);'''
# Expose the old behavior to the new input-contract assertions as well.
old_helper = '''static const float (*fetch_shader_vertex_inputs(uint32_t v))[4] {
static float input[16][4];memset(input,0,sizeof input);
for(unsigned i=0;i<16;i++)if(s_vertex_inputs&(1u<<i))fetch_vertex_input(i,v,input[i]);
return (const float (*)[4])input;}
'''
probe = r'''
    const unsigned formats[]={0,1,2,4,5,6},masks[]={1,0x319,0x1f19,0xffff};
    uint32_t rng=148;uint64_t hash=1469598103934665603ULL;
    s_gpu.inline_active=0;
    for(unsigned i=0;i<8192;i++) {
        rng=rng*1664525u+1013904223u;memcpy(ram+0x20000+i*4,&rng,4);
    }
    for(unsigned i=0;i<16;i++) {
        s_gpu.attr[i]=(VertexAttr){0x20001+i*1024,formats[i%6],1+i%4,i%4?0:16};
        if(i==15)s_gpu.attr[i].size=0;
        memcpy(s_gpu.imm_attr[i],ram+0x20000+i*16,16);
    }
    struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
    for(unsigned batch=0;batch<50000;batch++) {
        ++s_vertex_generation;s_vertex_inputs=masks[batch%4];
        memcpy(ram+0x20401,&batch,4);memcpy(s_gpu.imm_attr[15],&batch,4);
        for(unsigned v=0;v<64;v++) {
            /* ASSEMBLE */
            /* Hash all lanes in several vertices of every batch, including
             * unused lanes. Sample one varying lane on the remaining ones
             * so checksum work does not dominate the assembly measurement. */
            if(v<4) {
                uint32_t bits[64];memcpy(bits,input,sizeof bits);
                for(unsigned k=0;k<64;k++)hash=(hash^bits[k])*1099511628211ULL;
            } else {
                uint32_t bits;memcpy(&bits,&input[(v+batch)&15][(v^batch)&3],4);
                hash=(hash^bits)*1099511628211ULL;
            }
        }
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    printf("%016llx %.9f\n",(unsigned long long)hash,
           end.tv_sec-begin.tv_sec+(end.tv_nsec-begin.tv_nsec)*1e-9);
'''
report = {}
with tempfile.TemporaryDirectory(prefix='ssx-input-assembly-') as work:
    executables = {}
    for name, source in [('old', a.baseline.resolve()), ('new', root / 'third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c')]:
        c = Path(work) / (name + '.c')
        obj, exe = c.with_suffix('.o'), c.with_suffix('')
        assembly = old_assembly if name == 'old' else 'const float (*input)[4]=fetch_shader_vertex_inputs(v);'
        body = (root / 'tests/raster.c').read_text().replace(
            '#include "../third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c"',
            '#include <time.h>\n#include "' + str(source) + '"').replace(
            'int main(void)', (old_helper if name == 'old' else '') + 'int main(void)').replace(
            '    free(ram); return 0;', probe.replace('/* ASSEMBLE */', assembly) + '\n    free(ram); return 0;')
        c.write_text(body)
        cmd = compile.copy()
        cmd[cmd.index('-o') + 1], cmd[cmd.index('-c') + 1] = str(obj), str(c)
        if '-MF' in cmd:
            cmd[cmd.index('-MF') + 1] = str(c.with_suffix('.d'))
        if '-MT' in cmd:
            cmd[cmd.index('-MT') + 1] = str(obj)
        subprocess.run(cmd, cwd=build, check=True, stdout=subprocess.DEVNULL)
        cmd = link.copy()
        cmd[cmd.index('CMakeFiles/raster_test.dir/tests/raster.c.o')] = str(obj)
        cmd[cmd.index('-o') + 1] = str(exe)
        subprocess.run(cmd, cwd=build, check=True)
        executables[name] = exe
        report[name] = {'source': str(source), 'hash': None, 'elapsed_seconds': []}
    for run in range(a.runs):
        for name in (('old', 'new') if run % 2 == 0 else ('new', 'old')):
            result = subprocess.run([str(executables[name])], capture_output=True, text=True, check=True)
            digest, elapsed = result.stdout.split()
            if report[name]['hash'] not in (None, digest):
                raise SystemExit('Nondeterministic input bank')
            report[name]['hash'] = digest
            report[name]['elapsed_seconds'].append(float(elapsed))
    if report['old']['hash'] != report['new']['hash']:
        raise SystemExit('Shader input banks differ')
a.report.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
