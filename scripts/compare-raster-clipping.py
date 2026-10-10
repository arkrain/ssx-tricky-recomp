#!/usr/bin/env python3
from pathlib import Path
import subprocess,shlex
import argparse,tempfile,time,json
p=argparse.ArgumentParser(description='Compare 30,000 CPU triangle images and GL vertex streams against a prior nv2a_pb_exec.c snapshot.')
p.add_argument('--baseline',type=Path,required=True)
p.add_argument('--harness',type=Path,help='Compatible raster.c snapshot when new regressions intentionally fail on the baseline')
p.add_argument('--runs',type=int,default=1,help='Repeated comparison runs; elapsed times include process startup')
p.add_argument('--report',type=Path,help='Write hashes and individual elapsed times as JSON')
args=p.parse_args()
if args.runs<1:p.error('--runs must be positive')
root=Path(__file__).resolve().parents[1];build=root/'build/linux-release'
work=tempfile.TemporaryDirectory(prefix='ssx-clip-');folder=Path(work.name)
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','raster_test'],text=True).splitlines()
compile=next(shlex.split(x) for x in commands if ' -c ' in x and '/tests/raster.c' in x)
link=shlex.split(commands[-1])[2:-2];link=[x for x in link if not x.startswith('-Wl,--dependency-file=')]
probe=r'''
    s_skip_frame=0;s_render_every=1;s_gl_collect=0;s_gpu.depth_enable=0;s_gpu.alpha_enable=0;s_gpu.blend_enable=0;s_gpu.tex.valid=0;
    s_triangle_colors=0;
    uint32_t rng=17;uint64_t hash=1469598103934665603ULL;
    for(unsigned n=0;n<30000;n++) {
        float tri[3][4];
        for(int v=0;v<3;v++)for(int k=0;k<4;k++) {
            rng=rng*1664525u+1013904223u;
            float f=(rng>>8)*(1.0f/16777216.0f);
            tri[v][k]=k==3?(n%2?f*8+.01f:f*6-2):k==2?f*20-5:f*48-16;
            if(n%3==0)tri[v][k]=k==3?f*8+.01f:k==2?f*10:f*16;
        }
        memset(pixels,0,16*16*4);
        raster_shader_triangle(tri,0xff000000u|(n*7919));
        for(unsigned i=0;i<256;i++)hash=(hash^pixels[i])*1099511628211ULL;
    }

    printf("30000 triangle image hash: %016llx\n",(unsigned long long)hash);
    s_gl_collect=1;s_triangle_colors=1;hash=1469598103934665603ULL;
    for(unsigned n=0;n<30000;n++) {
        float tri[3][4];
        for(int v=0;v<3;v++)for(int k=0;k<4;k++) {
            rng=rng*1664525u+1013904223u;
            float f=(rng>>8)*(1.0f/16777216.0f);
            tri[v][k]=k==3?f*6-1:k==2?f*20-5:f*48-16;
            if(n%3==0)tri[v][k]=k==3?f*8+.01f:k==2?f*10:f*16;
        }
        if(n%101==0)tri[0][3]=0;
        if(n%103==0)tri[1][1]=NAN;
        if(n%107==0)tri[2][2]=INFINITY;
        for(int v=0;v<3;v++)for(int k=0;k<4;k++) {
            for(int c=0;c<2;c++) {
                rng=rng*1664525u+1013904223u;
                s_triangle_color[v][c][k]=(rng>>8)*(1.f/16777216.f);
            }
            for(int t=0;t<4;t++) {
                rng=rng*1664525u+1013904223u;
                s_triangle_tex[v][t][k]=(rng>>8)*(8.f/16777216.f)-2;
            }
        }
        s_gl_count=0;raster_shader_triangle(tri,0xffabcdef);
        hash=(hash^s_gl_count)*1099511628211ULL;
        for(size_t i=0;i<s_gl_count*sizeof(*s_gl_vertices);i++)
            hash=(hash^((unsigned char*)s_gl_vertices)[i])*1099511628211ULL;
    }
    printf("30000 triangle vertex stream hash: %016llx\n",(unsigned long long)hash);

'''
hashes=[];report={}
for name,source in [('old',args.baseline.resolve()),('new',root/'third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c')]:
 c=folder/(name+'.c');obj=c.with_suffix('.o');exe=c.with_suffix('')
 text=(args.harness or root/'tests/raster.c').read_text().replace('#include "../third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c"','#include "'+str(source)+'"').replace('    free(ram); return 0;',probe+'\n    free(ram); return 0;')
 c.write_text(text);cmd=compile.copy();cmd[cmd.index('-o')+1]=str(obj);cmd[cmd.index('-c')+1]=str(c)
 subprocess.run(cmd,cwd=build,check=True,stdout=subprocess.DEVNULL)
 cmd=link.copy();cmd[cmd.index('CMakeFiles/raster_test.dir/tests/raster.c.o')]=str(obj);cmd[cmd.index('-o')+1]=str(exe)
 subprocess.run(cmd,cwd=build,check=True)
 times=[];expected=None
 for run in range(args.runs):
  begin=time.perf_counter();result=subprocess.run([str(exe)],capture_output=True,text=True);times.append(time.perf_counter()-begin)
  if result.returncode:
   print(name,result.stdout,result.stderr,flush=True)
   result.check_returncode()
  if expected is not None and result.stdout!=expected:raise SystemExit('Nondeterministic clipping output')
  expected=result.stdout
 report[name]={'source':str(source),'output':result.stdout,'elapsed_seconds':times}
 print(name,result.stdout.strip());hashes.append(result.stdout)
if hashes[0]!=hashes[1]:raise SystemExit('Clipping images differ')
if args.report:args.report.write_text(json.dumps(report,indent=2)+'\n')
work.cleanup()
