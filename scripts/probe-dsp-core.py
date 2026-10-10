#!/usr/bin/env python3
"""Prepare pinned DSP sources or build their standalone interpreter probe.

--fetch downloads pinned upstream files; --fetch-missing reuses verified
cached inputs. --backend --prepare-only prepares normal-build sources without
compiling or running the standalone probe.
"""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import urllib.request

REVISION='f9b14039e5bb56ae2d8f028e31e7cc19f13f7e12'
PREFIX='hw/xbox/mcpx/apu/dsp'
FILES=('debug.h','trace.h','interp/dsp_cpu.c','interp/dsp_cpu.h',
       'interp/dsp_cpu_regs.h','interp/dsp_dis.c.inc','interp/dsp_emu.c.inc')
BACKEND_FILES=('dsp.c','dsp.h','dsp_c.c','dsp_internal.h',
               'dsp_dma.c','dsp_dma.h','dsp_dma_regs.h')
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--fetch',action='store_true')
p.add_argument('--fetch-missing',action='store_true',help='Download only absent pinned inputs; reject changed cached files')
p.add_argument('--prepare-only',action='store_true',help='Verify and prepare backend sources without compiling a probe')
p.add_argument('--cc',default='/usr/bin/gcc')
p.add_argument('--backend',action='store_true',help='Also exercise bootstrap, reset and DMA through the backend API')
p.add_argument('--sanitize',action='store_true',help='Enable AddressSanitizer and UndefinedBehaviorSanitizer')
p.add_argument('--unpatched',action='store_true',help='Use untouched upstream sources as a negative control')
a=p.parse_args()
if a.prepare_only and (not a.backend or a.unpatched or a.sanitize):
    p.error('--prepare-only requires --backend and cannot use --unpatched or --sanitize')
root=Path(__file__).resolve().parents[1]
work=root/'build/dsp-research';source=work/PREFIX
support=root/'tools/dsp-research'
expected=json.loads((support/'source-hashes.json').read_text())
if expected['revision']!=REVISION:p.error('Pinned source manifest revision mismatch')
hashes={}
for name in FILES+(BACKEND_FILES if a.backend else ()):
    target=source/name
    if a.fetch or (a.fetch_missing and not target.is_file()):
        data=urllib.request.urlopen(
            f'https://raw.githubusercontent.com/xemu-project/xemu/{REVISION}/{PREFIX}/{name}',
            timeout=30).read()
        if hashlib.sha256(data).hexdigest()!=expected['source_hashes'][name]:
            p.error('Downloaded source hash mismatch: '+name)
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    if not target.is_file():p.error('Missing upstream sources; run once with --fetch')
    hashes[name]=hashlib.sha256(target.read_bytes()).hexdigest()
    if hashes[name]!=expected['source_hashes'][name]:p.error('Upstream source hash mismatch: '+name)
patches={}
if a.backend and not a.unpatched:
    patched=work/'patched'
    for name in FILES+BACKEND_FILES:
        target=patched/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source/name,target)
    for patch in sorted((support/'patches').glob('*.patch')):
        subprocess.run(['patch','--batch','-p1','-i',str(patch)],cwd=patched,check=True)
        patches[patch.name]=hashlib.sha256(patch.read_bytes()).hexdigest()
    source=patched
if a.prepare_only:
    print('Prepared pinned DSP backend at '+str(source))
    raise SystemExit(0)
probe='backend_probe' if a.backend else 'core_probe'
exe=work/probe
command=[a.cc,'-std=gnu11','-O2','-I'+str(support/'shim'),
         '-I'+str(source),'-I'+str(source/'interp'),str(support/(probe+'.c')),
         str(source/'interp/dsp_cpu.c'),'-lm','-o',str(exe)]
if a.backend:command[1:1]=[str(source/name) for name in ('dsp.c','dsp_c.c','dsp_dma.c')]
if a.backend:command[1:1]=['-I'+str(root/'src'),*[str(root/'src'/name) for name in
    ('apu_dsp_backend.c','apu_dsp_mmio.c','apu_dsp_dma.c')]]
if a.sanitize:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
subprocess.run(command,check=True)
env=os.environ.copy()
if a.sanitize:env['UBSAN_OPTIONS']=env.get('UBSAN_OPTIONS','')+':halt_on_error=1'
result=subprocess.run([str(exe)],capture_output=True,text=True,env=env)
if result.returncode:
    print(result.stdout,result.stderr)
    result.check_returncode()
report={'revision':REVISION,'license':'GPL-2.0-or-later',
        'source_hashes':hashes,'patches':patches,'command':command,'output':result.stdout,
        'scope':'Standalone backend/DMA integration checks' if a.backend else
        'Standalone memory/NOP integration smoke; not DSP conformance or game effects'}
(root/('reports/dsp123-'+probe.replace('_','-')+('-sanitized' if a.sanitize else '')+'.json')).write_text(json.dumps(report,indent=2)+'\n')
print(result.stdout,end='')
