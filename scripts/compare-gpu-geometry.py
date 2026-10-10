#!/usr/bin/env python3
"""Compare production CPU/GPU draw replay buffers; keep captured assets local."""
import argparse,hashlib,importlib.util,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--capture',type=Path,required=True)
p.add_argument('--report-prefix',type=Path,required=True)
p.add_argument('--build-dir',type=Path,default=Path('build/linux-release'))
p.add_argument('--replay-binary',type=Path,help='Explicit archived replay executable; otherwise rebuild the draw_replay target')
p.add_argument('--minimum-indices',type=int,default=3)
p.add_argument('--runs',type=int,default=1)
p.add_argument('--warmup',type=int,default=3)
p.add_argument('--scenario',choices=['normal','nonfinite-constant','nonfinite-input'],default='normal')
p.add_argument('--gpu-first',action='store_true')
a=p.parse_args();root=Path(__file__).resolve().parents[1]
a.report_prefix.parent.mkdir(parents=True,exist_ok=True)
if a.replay_binary is None:
 subprocess.run(['cmake','--build',str(a.build_dir.resolve()),'--target','draw_replay','--parallel','4'],check=True,stdout=subprocess.DEVNULL)
replay=(a.replay_binary or a.build_dir/'draw_replay').resolve()
if not replay.is_file():p.error(f'replay executable does not exist: {replay}')
replay_hash=hashlib.sha256(replay.read_bytes()).hexdigest()
spec=importlib.util.spec_from_file_location('launcher',root/'scripts/play-linux.py');launcher=importlib.util.module_from_spec(spec);spec.loader.exec_module(launcher)
env=dict(os.environ,SSX_GL='1',SSX_VSH_NATIVE='1',SSX_GL_SPECIALIZE='1',SSX_REPLAY_OPTIONS='clear '+a.scenario,SSX_REPLAY_RUNS=str(a.runs),SSX_REPLAY_WARMUP=str(a.warmup),SSX_GL_GPU_GEOMETRY_MIN_INDICES=str(a.minimum_indices))
env.pop('SSX_VSH_TRACE',None)
launcher.matching_nvidia_libraries(env,a.build_dir.resolve()/'gpu-geometry-env')
report={'replay_binary':str(replay),'replay_sha256':replay_hash,'scenario':a.scenario,'capture':str(a.capture.resolve()),'minimum_indices':a.minimum_indices,'warmup':a.warmup,'runs':a.runs,'gpu_first':a.gpu_first}
for name in (['gpu','cpu'] if a.gpu_first else ['cpu','gpu']):
 prefix=str(a.report_prefix.resolve())+'-'+name
 env.update(SSX_GL_GPU_GEOMETRY=str(int(name=='gpu')),SSX_REPLAY_OUTPUT=prefix,RECOMP_FB_DUMP=prefix)
 with open(prefix+'.jsonl','w') as output,open(prefix+'.log','w') as errors:
  result=subprocess.run([str(replay),str(a.capture.resolve())],env=env,stdout=output,stderr=errors)
 if result.returncode:raise SystemExit(f'{name} failed {result.returncode}: {prefix}.log')
 report[name]=[json.loads(x) for x in Path(prefix+'.jsonl').read_text().splitlines()]
for kind in ['color','depth']:
 blobs=[Path(str(a.report_prefix.resolve())+'-'+name+'.'+kind+'.bin').read_bytes() for name in ['cpu','gpu']]
 assert len(blobs[0])==len(blobs[1])
 report[kind]={'bytes':len(blobs[0]),'different_words':sum(blobs[0][i:i+4]!=blobs[1][i:i+4] for i in range(0,len(blobs[0]),4)),'sha256':[hashlib.sha256(b).hexdigest() for b in blobs]}
Path(str(a.report_prefix)+'.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
raise SystemExit(int(any(report[k]['different_words'] for k in ['color','depth'])))
