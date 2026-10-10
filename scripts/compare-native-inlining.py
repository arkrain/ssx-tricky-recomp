#!/usr/bin/env python3
"""Compare native opcode inlining using real shader captures and the interpreter test.

Run with the game closed. Only temporary native objects change; production
source/build outputs remain untouched. Timings measure native shader calls,
not complete rendering or FPS. Requires a configured GCC/Clang Ninja build.
"""
import argparse
import json
import os
import shlex
import statistics
import subprocess
import tempfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--captures', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
p.add_argument('--build-dir', type=Path)
p.add_argument('--runs', type=int, default=7)
p.add_argument('--programs', type=int, nargs='+', default=[7, 16])
a = p.parse_args()
if a.runs < 1:
    p.error('runs must be positive')
if not 1 <= len(a.programs) <= 22 or any(n < 0 for n in a.programs):
    p.error('select one to 22 nonnegative program numbers')
root = Path(__file__).resolve().parents[1]
build = a.build_dir.resolve() if a.build_dir else root / 'build/linux-release'
commands = subprocess.check_output(
    ['ninja', '-C', str(build), '-t', 'commands', 'vertex_native_test'], text=True).splitlines()
compile = next(shlex.split(c) for c in commands
               if ' -c ' in c and '/src/nv2a_vertex_native.c' in c)
link = [x for x in shlex.split(commands[-1])[2:-2]
        if not x.startswith('-Wl,--dependency-file=')]
runner = r'''
#include "nv2a_vertex_native.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static size_t load(const char *dir,unsigned program,const char *kind,void *out,size_t cap) {
    char name[4096];snprintf(name,sizeof name,"%s/program%u-%s.bin",dir,program,kind);
    FILE *f=fopen(name,"rb");assert(f);size_t n=fread(out,1,cap,f);
    assert(!ferror(f) && fgetc(f)==EOF);fclose(f);return n;
}
int main(int argc,char **argv) {
    assert(argc>=3 && argc<=24);unsigned count=argc-2;
    float initial[22][64],input[22][64],context[22][768],output[52];
    SsxNativeVertexFn fn[22];
    for(unsigned p=0;p<count;p++) {
        unsigned program=strtoul(argv[p+2],NULL,10);
        uint32_t code[136][4];size_t bytes=load(argv[1],program,"code",code,sizeof code);
        assert(bytes && !(bytes%16));fn[p]=ssx_vertex_native_find(code,bytes/16);assert(fn[p]);
        assert(load(argv[1],program,"input",initial[p],sizeof initial[p])==sizeof initial[p]);
        assert(load(argv[1],program,"constants",context[p],sizeof context[p])==sizeof context[p]);
        memcpy(input[p],initial[p],sizeof input[p]);
    }
    uint64_t hash=1469598103934665603ULL;
    struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
    for(unsigned vertex=0;vertex<1000000;vertex++)for(unsigned p=0;p<count;p++) {
        /* Small position changes retain the captured context while avoiding
         * a repeated constant-output microbenchmark. */
        input[p][0]=initial[p][0]+(vertex%257)*.0009765625f;
        memset(output,0,sizeof output);for(unsigned r=0;r<13;r++)output[r*4+3]=1;
        int ok=fn[p](input[p],context[p],output);assert(ok);
        if(vertex%64==0)for(unsigned k=0;k<sizeof output;k++)
            hash=(hash^((unsigned char*)output)[k])*1099511628211ULL;
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    printf("%016llx %.9f\n",(unsigned long long)hash,
           end.tv_sec-begin.tv_sec+(end.tv_nsec-begin.tv_nsec)*1e-9);
    return 0;
}
'''
report = {'scope': f'{len(a.programs)} million native shader calls per run; not FPS',
          'programs': a.programs, 'runs': a.runs}
with tempfile.TemporaryDirectory(prefix='ssx-native-inline-') as work:
    executables = {}
    for variant in ('baseline', 'inline'):
        directory = Path(work) / variant
        directory.mkdir()
        native = directory / 'native.c'
        native.write_text((root / 'src/nv2a_vertex_native.c').read_text())
        ops = (build / 'nv2a_native_ops.inc').read_text()
        ops = ops.replace('static inline __attribute__((always_inline)) void ssx_native_op_',
                          'static inline void ssx_native_op_')
        if variant == 'inline':
            assert 'static inline void ssx_native_op_' in ops
            ops = ops.replace('static inline void ssx_native_op_',
                              'static inline __attribute__((always_inline)) void ssx_native_op_')
        (directory / 'nv2a_native_ops.inc').write_text(ops)
        obj = directory / 'native.o'
        cmd = compile.copy()
        for flag, value in [('-o', obj), ('-c', native), ('-MF', directory / 'native.d'), ('-MT', obj)]:
            if flag in cmd:
                cmd[cmd.index(flag) + 1] = str(value)
        subprocess.run(cmd, cwd=build, check=True, stdout=subprocess.DEVNULL)
        test = directory / 'equivalence'
        cmd = link.copy()
        archive = next(i for i, item in enumerate(cmd) if item.endswith('libssx_vertex.a'))
        cmd.insert(archive, str(obj))
        cmd[cmd.index('-o') + 1] = str(test)
        subprocess.run(cmd, cwd=build, check=True)
        test_env = dict(os.environ, SSX_VSH_NATIVE='1')
        test_env.pop('SSX_VSH_TRACE', None)
        tested = subprocess.run([str(test), str(root / 'game/default.xbe')], env=test_env,
                                capture_output=True, text=True, check=True)
        c = directory / 'runner.c'
        c.write_text(runner)
        exe = directory / 'runner'
        subprocess.run([compile[0], '-O2', '-std=gnu11', '-I' + str(root / 'src'),
                        str(c), str(obj), '-lm', '-o', str(exe)], check=True)
        executables[variant] = exe
        report[variant] = {'equivalence': tested.stdout.strip(), 'hash': None, 'elapsed_seconds': []}
    for run in range(a.runs):
        for variant in (('baseline', 'inline') if run % 2 == 0 else ('inline', 'baseline')):
            measured = subprocess.run([str(executables[variant]), str(a.captures.resolve()),
                                       *map(str, a.programs)],
                                      capture_output=True, text=True, check=True)
            digest, seconds = measured.stdout.split()
            assert report[variant]['hash'] in (None, digest), 'Nondeterministic output'
            report[variant]['hash'] = digest
            report[variant]['elapsed_seconds'].append(float(seconds))
    assert report['baseline']['hash'] == report['inline']['hash'], 'Native outputs differ'
    for variant in ('baseline', 'inline'):
        report[variant]['median_seconds'] = statistics.median(report[variant]['elapsed_seconds'])
a.report.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
