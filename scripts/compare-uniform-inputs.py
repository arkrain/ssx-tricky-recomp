#!/usr/bin/env python3
"""Compare decoded batch inputs and isolated timing with an uncached snapshot.

Run with the game closed. This is an input-fetch microbenchmark, not FPS.
The existing raster assertions run before the synthetic mixed-format batches.
"""
import argparse, json, shlex, subprocess, tempfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
p.add_argument('--runs', type=int, default=7)
a = p.parse_args()
if a.runs < 1: p.error('runs must be positive')
root = Path(__file__).resolve().parents[1]
build = root / 'build/linux-release'
commands = subprocess.check_output(['ninja','-C',str(build),'-t','commands','raster_test'],text=True).splitlines()
compile = next(shlex.split(x) for x in commands if ' -c ' in x and '/tests/raster.c' in x)
link = [x for x in shlex.split(commands[-1])[2:-2] if not x.startswith('-Wl,--dependency-file=')]
probe = r'''
    const unsigned formats[]={0,1,2,4,5,6};
    uint32_t rng=129;uint64_t hash=1469598103934665603ULL;
    s_gpu.inline_active=0;
    for(unsigned i=0;i<4096;i++) {
        rng=rng*1664525u+1013904223u;memcpy(ram+0x20000+i*4,&rng,4);
    }
    for(unsigned i=0;i<16;i++) {
        s_gpu.attr[i]=(VertexAttr){0x20000+i*1024,formats[i%6],1+i%4,i%4?0:16};
        if(i==15)s_gpu.attr[i].size=0;
        memcpy(s_gpu.imm_attr[i],ram+0x20000+i*16,16);
    }
    struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
    for(unsigned batch=0;batch<25000;batch++) {
        ++s_vertex_generation;
        /* Change broadcast memory and disabled-array fallback every batch. */
        memcpy(ram+0x20400,&batch,4);memcpy(s_gpu.imm_attr[15],&batch,4);
        for(unsigned v=0;v<64;v++)for(unsigned i=0;i<16;i++) {
            float out[4];uint32_t bits[4];probe_input(i,v,out);memcpy(bits,out,16);
            for(unsigned k=0;k<4;k++)hash=(hash^bits[k])*1099511628211ULL;
        }
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    printf("%016llx %.9f\n",(unsigned long long)hash,
           end.tv_sec-begin.tv_sec+(end.tv_nsec-begin.tv_nsec)*1e-9);
'''
report = {}
with tempfile.TemporaryDirectory(prefix='ssx-uniform-') as work:
    executables = {}
    for name, source in [('old',a.baseline.resolve()),('new',root/'third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c')]:
        c = Path(work)/(name+'.c');obj=c.with_suffix('.o');exe=c.with_suffix('')
        helper = ('static void probe_input(unsigned a,uint32_t i,float out[4]) {'
                  'if(!fetch_attr(&s_gpu.attr[a],i,out))memcpy(out,s_gpu.imm_attr[a],16);}\n'
                  if name=='old' else '#define probe_input fetch_vertex_input\n')
        text = (root/'tests/raster.c').read_text().replace(
            '#include "../third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c"',
            '#include <time.h>\n#include "'+str(source)+'"').replace(
            'int main(void)',helper+'int main(void)').replace(
            '    free(ram); return 0;',probe+'\n    free(ram); return 0;')
        c.write_text(text);cmd=compile.copy()
        cmd[cmd.index('-o')+1]=str(obj);cmd[cmd.index('-c')+1]=str(c)
        subprocess.run(cmd,cwd=build,check=True,stdout=subprocess.DEVNULL)
        cmd=link.copy();cmd[cmd.index('CMakeFiles/raster_test.dir/tests/raster.c.o')]=str(obj)
        cmd[cmd.index('-o')+1]=str(exe);subprocess.run(cmd,cwd=build,check=True)
        executables[name]=exe;report[name]={'source':str(source),'hash':None,'elapsed_seconds':[]}
    for run in range(a.runs):
        for name in (('old','new') if run%2==0 else ('new','old')):
            result=subprocess.run([str(executables[name])],capture_output=True,text=True,check=True)
            digest,elapsed=result.stdout.split()
            if report[name]['hash'] not in (None,digest):raise SystemExit('Nondeterministic input output')
            report[name]['hash']=digest;report[name]['elapsed_seconds'].append(float(elapsed))
    if report['old']['hash']!=report['new']['hash']:raise SystemExit('Decoded inputs differ')
a.report.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
