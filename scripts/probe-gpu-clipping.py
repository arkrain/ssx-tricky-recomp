#!/usr/bin/env python3
"""Offline comparison of ordered GPU clipping with the production CPU path."""
import argparse
import importlib.util
import os
import shlex
import subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir',type=Path,default=Path('build/linux-release'))
p.add_argument('--output-dir',type=Path,default=Path('build/gpu-clipping159'))
p.add_argument('--report-prefix',type=Path,required=True)
p.add_argument('--trials',type=int,default=3000)
p.add_argument('--pixel-cases',type=int,default=0)
p.add_argument('--benchmark',action='store_true')
p.add_argument('--geometry',type=Path,help='Alternate offline geometry shader for controlled comparisons')
a=p.parse_args()
if not 1<=a.trials<=1000000:p.error('trials must be between1 and1000000')
if not 0<=a.pixel_cases<=a.trials:p.error('pixel-cases must be between0 and trials')
root=Path(__file__).resolve().parents[1];build=a.build_dir.resolve();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
a.report_prefix.parent.mkdir(parents=True,exist_ok=True)
shader=out/'clip.geom'
geometry=a.geometry.resolve() if a.geometry else root/'src/nv2a_gpu_clip.glsl'
shader.write_text('#version 330 core\n#extension GL_ARB_gpu_shader5 : require\n#extension GL_ARB_gpu_shader_fp64 : require\n'+(root/'src/nv2a_gpu_ops.glsl').read_text()+'\n'+geometry.read_text())
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','raster_test'],text=True).splitlines()
compile=next(shlex.split(x) for x in commands if ' -c ' in x and '/tests/raster.c' in x)
obj=out/'probe.o'
for flag,value in [('-c',root/'tools/probe_gpu_clipping.c'),('-o',obj),('-MF',out/'probe.d'),('-MT',obj)]:
 if flag in compile:compile[compile.index(flag)+1]=str(value)
subprocess.run(compile,cwd=build,check=True)
link=[x for x in shlex.split(commands[-1])[2:-2] if not x.startswith('-Wl,--dependency-file=')]
link[link.index('CMakeFiles/raster_test.dir/tests/raster.c.o')]=str(obj)
exe=out/'probe';link[link.index('-o')+1]=str(exe)
subprocess.run(link,cwd=build,check=True)
spec=importlib.util.spec_from_file_location('launcher',root/'scripts/play-linux.py');launcher=importlib.util.module_from_spec(spec);spec.loader.exec_module(launcher)
env=dict(os.environ,SSX_GL='1',SSX_CLIP_PIXEL_CASES=str(a.pixel_cases))
env.pop('SSX_CLIP_BENCH',None)
if a.benchmark:env['SSX_CLIP_BENCH']='1'
launcher.matching_nvidia_libraries(env,out/'env')
with open(str(a.report_prefix)+'.json','w') as stdout,open(str(a.report_prefix)+'.log','w') as stderr:
 result=subprocess.run([str(exe),str(shader),str(a.trials)],env=env,stdout=stdout,stderr=stderr)
print('GPU clipping probe exit',result.returncode)
raise SystemExit(result.returncode)
