#!/usr/bin/env python3
"""Compare exact captured shader input banks and isolated preparation cost.

Uses local asset captures; does not render or measure game frame rate.
"""
import argparse, hashlib, json, shlex, subprocess, tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline',type=Path,required=True)
p.add_argument('--capture',type=Path,required=True)
p.add_argument('--report',type=Path,required=True)
p.add_argument('--runs',type=int,default=7)
p.add_argument('--batches',type=int,default=2000)
a=p.parse_args()
if not 1<=a.runs<=31 or not 1<=a.batches<=100000:p.error('invalid run/batch count')
root=Path(__file__).resolve().parents[1];build=root/'build/linux-release'
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','draw_replay'],text=True).splitlines()
compile=next(shlex.split(x) for x in commands if ' -c ' in x and '/tools/draw_replay.c' in x)
link=[x for x in shlex.split(commands[-1])[2:-2] if not x.startswith('-Wl,--dependency-file=')]
probe=r'''
    s_vertex_inputs=ssx_vertex_input_mask();
    uint64_t hash=1469598103934665603ULL;
    ++s_vertex_generation;
    for(unsigned i=0;i<s_gpu.idx_count;i++) {
        const unsigned char *bank=(const unsigned char *)fetch_shader_vertex_inputs(s_gpu.idx[i]);
        for(unsigned k=0;k<sizeof s_shader_inputs;k++)hash=(hash^bank[k])*1099511628211ULL;
    }
    struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
    for(unsigned batch=0;batch<PROBE_BATCHES;batch++) {
        ++s_vertex_generation;
        for(unsigned i=0;i<s_gpu.idx_count;i++) {
            const float (*bank)[4]=fetch_shader_vertex_inputs(s_gpu.idx[i]);
            /* Model an opaque read-only consumer without measuring shader
             * arithmetic or hashing; force every bank to be materialized. */
            __asm__ volatile("" : : "r"(bank) : "memory");
        }
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    printf("%016llx %.9f %u %u\n",(unsigned long long)hash,
        end.tv_sec-begin.tv_sec+(end.tv_nsec-begin.tv_nsec)*1e-9,
        s_gpu.idx_count,s_vertex_inputs);
    return 0;
'''.replace('PROBE_BATCHES',str(a.batches))
report={'capture':str(a.capture.resolve()),'batches':a.batches}
with tempfile.TemporaryDirectory(prefix='ssx-input-compare-') as work:
 executables={}
 for name,source in [('old',a.baseline.resolve()),('new',root/'third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c')]:
  c=Path(work)/(name+'.c');obj=c.with_suffix('.o');exe=c.with_suffix('')
  body=(root/'tools/draw_replay.c').read_text().replace('#include "../third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c"','#include "'+str(source)+'"').replace('#include "../src/nv2a_vertex.c"','#include "'+str(root/'src/nv2a_vertex.c')+'"')
  assert '    ssx_gl_sync();' in body
  c.write_text(body.replace('    ssx_gl_sync();',probe+'\n    ssx_gl_sync();',1))
  cmd=compile.copy()
  for flag,value in [('-c',c),('-o',obj),('-MF',c.with_suffix('.d')),('-MT',obj)]:
   if flag in cmd:cmd[cmd.index(flag)+1]=str(value)
  subprocess.run(cmd,cwd=build,check=True,stdout=subprocess.DEVNULL)
  cmd=link.copy();cmd[cmd.index('CMakeFiles/draw_replay.dir/tools/draw_replay.c.o')]=str(obj);cmd[cmd.index('-o')+1]=str(exe)
  subprocess.run(cmd,cwd=build,check=True)
  executables[name]=exe;report[name]={'source':str(source),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'hash':None,'elapsed_seconds':[]}
 for run in range(a.runs):
  for name in (['old','new'] if run%2==0 else ['new','old']):
   result=subprocess.run([str(executables[name]),str(a.capture.resolve())],capture_output=True,text=True,check=True)
   digest,elapsed,indices,mask=result.stdout.split()
   if report[name]['hash'] not in (None,digest):raise SystemExit('Nondeterministic input bank')
   report[name]['hash']=digest;report[name]['elapsed_seconds'].append(float(elapsed));report['indices']=int(indices);report['input_mask']=int(mask)
 if report['old']['hash']!=report['new']['hash']:raise SystemExit('Shader input banks differ')
a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
