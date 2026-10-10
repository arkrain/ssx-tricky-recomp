#!/usr/bin/env python3
"""Audit tracked publication files without reading ignored game assets."""
import re
import subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
allowed={'.github','docs','LICENSES','packaging','patches','scripts','src','tests','tools'}
root_files={'.gitignore','README.md','LICENSE','THIRD_PARTY.md','CONTRIBUTING.md',
            'CMakeLists.txt','CMakePresets.json','requirements.txt','play'}
paths=subprocess.check_output(['git','ls-files','-z'],cwd=root).decode().split('\0')
errors=[]
for name in filter(None,paths):
 p=Path(name)
 if p.parts[0] not in allowed and name not in root_files:errors.append(name+': unexpected path')
 if p.parts[:3]==('src','recomp','gen'):errors.append(name+': generated game code')
 f=root/p
 if f.is_symlink():errors.append(name+': symlink')
 data=f.read_bytes()
 if len(data)>1024*1024:errors.append(name+': oversized source file')
 if b'\0' in data or data.startswith(b'\x7fELF') or data.startswith(b'MZ'):errors.append(name+': binary file')
 if re.search(rb'(gh[pousr]_[A-Za-z0-9]{30,}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----)',data):errors.append(name+': credential marker')
 if re.search(rb'/home/[A-Za-z0-9_-]+/',data):errors.append(name+': personal path')
if errors:raise SystemExit('\n'.join(errors))
print('Public source audit passed: '+str(len(list(filter(None,paths))))+' tracked files')
