#!/usr/bin/env python3
"""Build and run the isolated GLSL vertex/interpreter comparison.

No production object is modified. Exit1 means output differences, exit2 a
probe/compiler error, and exit77 unavailable graphics support. Reports are
retained even when comparisons fail. Run with gameplay closed.
"""
import argparse
import importlib.util
import json
import os
import shlex
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir', type=Path, default=Path('build/linux-release'))
p.add_argument('--output-dir', type=Path, default=Path('build/gpu-vertex157'))
p.add_argument('--report-prefix', type=Path, required=True)
p.add_argument('--program', type=int)
p.add_argument('--captures', type=Path)
p.add_argument('--trials', type=int)
p.add_argument('--trace-step', type=int, help='Compare one operation against generated CPU opcode arithmetic')
p.add_argument('--difference-dir', type=Path, help='Local-only original game data for finite-output failures')
p.add_argument('--benchmark', action='store_true')
p.add_argument('--wide-values', action='store_true', help='Random finite float bit patterns, including subnormals and extreme exponents')
p.add_argument('--seed', type=lambda x: int(x, 0), default=0x535358)
a = p.parse_args()
if (a.program is None) != (a.captures is None):
    p.error('--program and --captures must be supplied together')
if a.trials is not None and not 1 <= a.trials <= 1000000:
    p.error('--trials must be between 1 and 1000000')
if a.program is not None and not 0 <= a.program < 22:
    p.error('--program must be between 0 and 21')
if a.benchmark and a.captures is None:
    p.error('--benchmark requires captured inputs/constants')
if a.trace_step is not None and (a.trace_step < 0 or a.captures is None or a.benchmark):
    p.error('--trace-step requires captures and cannot benchmark')
root = Path(__file__).resolve().parents[1]
build, output = a.build_dir.resolve(), a.output_dir.resolve()
output.mkdir(parents=True, exist_ok=True)
a.report_prefix.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root/'scripts/generate-vertex-glsl-probe.py'),
                '--native-inc', str(build/'nv2a_native_programs.inc'),
                '--output-dir', str(output)]+(['--trace-step',str(a.trace_step)] if a.trace_step is not None else []), check=True)
commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands',
                                     'gl_raster_test'], text=True).splitlines()
compile = next(shlex.split(x) for x in commands if ' -c ' in x and '/tests/gl_raster.c' in x)
if a.trace_step is not None:
    manifest = json.loads((output/'manifest.json').read_text())
    operations = next(x['operations'] for x in manifest if x['program']==a.program)
    if a.trace_step >= len(operations):
        p.error('--trace-step outside selected program')
    print(f'Trace operation {a.trace_step}: {operations[a.trace_step]} (generated CPU opcode reference)')
    compile.append('-DSSX_PROBE_TRACE')
obj = output/'probe.o'
for flag, value in [('-c', root/'tools/probe_gpu_vertex.c'), ('-o', obj),
                    ('-MF', output/'probe.d'), ('-MT', obj)]:
    if flag in compile:
        compile[compile.index(flag)+1] = str(value)
subprocess.run(compile, cwd=build, check=True)
link = [x for x in shlex.split(commands[-1])[2:-2]
        if not x.startswith('-Wl,--dependency-file=')]
index = next(i for i,x in enumerate(link) if x.endswith('tests/gl_raster.c.o'))
link[index] = str(obj)
link.insert(index+1, str(build/'CMakeFiles/vertex_native_test.dir/tests/vertex_reference.c.o'))
if a.trace_step is not None:
    trace_compile = compile.copy()
    trace_obj = output/'trace_native.o'
    for flag,value in [('-c',output/'trace_native.c'),('-o',trace_obj),
                       ('-MF',output/'trace_native.d'),('-MT',trace_obj)]:
        if flag in trace_compile:
            trace_compile[trace_compile.index(flag)+1] = str(value)
    trace_compile += ['-I'+str(build), '-I'+str(root/'third_party/nv2a_vsh_cpu/src')]
    subprocess.run(trace_compile,cwd=build,check=True)
    link.insert(index+1,str(trace_obj))
link.insert(link.index('-lm'), str(build/'libssx_vertex.a'))
exe = output/'probe'
link[link.index('-o')+1] = str(exe)
subprocess.run(link, cwd=build, check=True)
spec = importlib.util.spec_from_file_location('play_linux', root/'scripts/play-linux.py')
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)
env = dict(os.environ, SSX_GL='1')
env.pop('SSX_VSH_TRACE', None)
env['SSX_PROBE_SEED'] = str(a.seed)
env.pop('SSX_PROBE_BENCH', None)
env.pop('SSX_PROBE_WIDE', None)
env.pop('SSX_PROBE_DIFFERENCE_DIR', None)
if a.difference_dir:
    a.difference_dir.mkdir(parents=True, exist_ok=True)
    env['SSX_PROBE_DIFFERENCE_DIR'] = str(a.difference_dir.resolve())
if a.wide_values:
    env['SSX_PROBE_WIDE'] = '1'
if a.benchmark:
    env['SSX_PROBE_BENCH'] = '1'
if a.trials is not None:
    env['SSX_PROBE_TRIALS'] = str(a.trials)
else:
    env.pop('SSX_PROBE_TRIALS', None)
launcher.matching_nvidia_libraries(env, output/'env')
command = [str(exe), str(output)]
if a.program is not None:
    command += [str(a.program), str(a.captures.resolve())]
prefix = str(a.report_prefix)
with open(prefix+'.jsonl', 'w') as out, open(prefix+'.log', 'w') as err:
    result = subprocess.run(command, env=env, stdout=out, stderr=err)
print(f'GPU vertex probe exit {result.returncode}; reports {prefix}.jsonl/.log')
raise SystemExit(result.returncode)
