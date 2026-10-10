#!/usr/bin/env python3
"""Compare indexed GL vertex streams and isolated CPU work with a snapshot.

Run with the game closed. This synthetic benchmark does not measure FPS.
The baseline-compatible raster harness is required so new cache assertions
do not prevent comparison with an implementation lacking that cache.
"""
import argparse, json, shlex, subprocess, tempfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline',type=Path,required=True)
p.add_argument('--harness',type=Path,required=True)
p.add_argument('--report',type=Path,required=True)
p.add_argument('--runs',type=int,default=7)
a=p.parse_args()
if a.runs<1:p.error('runs must be positive')
root=Path(__file__).resolve().parents[1];build=root/'build/linux-release'
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','raster_test'],text=True).splitlines()
compile=next(shlex.split(x) for x in commands if ' -c ' in x and '/tests/raster.c' in x)
link=[x for x in shlex.split(commands[-1])[2:-2] if not x.startswith('-Wl,--dependency-file=')]
probe=r'''
    memset(&s_gpu,0,sizeof s_gpu);memset(s_render_reg,0,sizeof s_render_reg);
    s_gpu.clip_w=s_gpu.clip_h=16;s_gpu.pitch=64;s_gpu.color_offset=0x300000;
    s_gpu.attr[0]=(VertexAttr){0x1000,2,4,16};s_gpu.tex.color=6;
    ssx_vertex_method(0x1e94,6);ssx_vertex_method(0x1e9c,0);ssx_vertex_method(0x1ea0,0);
    for(int k=0;k<4;k++)ssx_vertex_method(0xb00+k*4,mov[k]);
    s_vertex_inputs=ssx_vertex_input_mask();
    uint64_t hash=1469598103934665603ULL;
    struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
    for(unsigned batch=0;batch<2000;batch++) {
        ++s_vertex_generation;s_gpu.clip_x=batch%3;s_gpu.clip_y=batch%2;
        float far=8+(batch%5);memcpy(&s_render_reg[0x398/4],&far,4);
        for(unsigned v=0;v<64;v++) {
            float position[4]={(v%8)*2.7f-1,(v/8)*2.7f-1,(v%11)*1.3f,
                              batch%4 ? 1+(v%4)*.3f : (v%3)-.5f};
            if(batch%17==0 && v==0)position[0]=NAN;
            if(batch%19==0 && v==1)position[0]=INFINITY;
            if(batch%23==0 && v==2)position[0]=3e38f;
            memcpy(ram+0x1000+v*16,position,16);
        }
        for(unsigned repeat=0;repeat<8;repeat++)for(unsigned v=0;v<55;v++) {
            if(v%8==7)continue;
            s_gl_collect=1;s_gl_count=0;
            raster_indexed(v,v+1,v+8,0xffffffff);
            /* Hash the initial pass and a cache-hit pass; the remaining
             * repetitions measure rendering work without checksum cost. */
            if(repeat<2) {
                hash=(hash^s_gl_count)*1099511628211ULL;
                for(size_t k=0;k<s_gl_count*sizeof(*s_gl_vertices);k++)
                    hash=(hash^((unsigned char*)s_gl_vertices)[k])*1099511628211ULL;
            }
        }
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    printf("%016llx %.9f\n",(unsigned long long)hash,
           end.tv_sec-begin.tv_sec+(end.tv_nsec-begin.tv_nsec)*1e-9);
'''
report={}
with tempfile.TemporaryDirectory(prefix='ssx-indexed-clip-') as work:
    executables={}
    for name,source in [('old',a.baseline.resolve()),('new',root/'third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c')]:
        c=Path(work)/(name+'.c');obj=c.with_suffix('.o');exe=c.with_suffix('')
        body=a.harness.read_text().replace(
            '#include "../third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c"',
            '#include <time.h>\n#include "'+str(source)+'"').replace(
            '    free(ram); return 0;',probe+'\n    free(ram); return 0;')
        c.write_text(body);cmd=compile.copy()
        cmd[cmd.index('-o')+1]=str(obj);cmd[cmd.index('-c')+1]=str(c)
        if '-MF' in cmd:cmd[cmd.index('-MF')+1]=str(c.with_suffix('.d'))
        if '-MT' in cmd:cmd[cmd.index('-MT')+1]=str(obj)
        subprocess.run(cmd,cwd=build,check=True,stdout=subprocess.DEVNULL)
        cmd=link.copy();cmd[cmd.index('CMakeFiles/raster_test.dir/tests/raster.c.o')]=str(obj)
        cmd[cmd.index('-o')+1]=str(exe);subprocess.run(cmd,cwd=build,check=True)
        executables[name]=exe;report[name]={'source':str(source),'hash':None,'elapsed_seconds':[]}
    for run in range(a.runs):
        for name in (('old','new') if run%2==0 else ('new','old')):
            result=subprocess.run([str(executables[name])],capture_output=True,text=True,check=True)
            digest,elapsed=result.stdout.split()
            if report[name]['hash'] not in (None,digest):raise SystemExit('Nondeterministic output')
            report[name]['hash']=digest;report[name]['elapsed_seconds'].append(float(elapsed))
    if report['old']['hash']!=report['new']['hash']:raise SystemExit('Indexed vertex streams differ')
a.report.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
