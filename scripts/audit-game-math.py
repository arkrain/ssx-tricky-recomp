#!/usr/bin/env python3
"""Run the generated game's actual CRT math code in an isolated memory image.
Requires a completed Linux/Ninja build and the user's extracted game files.
No game loop, device threads, or save writes are started.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root/'build/linux')
args = parser.parse_args()
build = args.build_dir.resolve()
commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'ssx_tricky'], text=True)
link = shlex.split(commands.splitlines()[-1])
if link[:2] != [':', '&&'] or link[-2:] != ['&&', ':']:
    raise SystemExit('Unexpected Ninja link command; inspect it before running the probe.')
link = [a for a in link[2:-2] if not a.startswith('-Wl,--dependency-file=')]
main = 'CMakeFiles/ssx_tricky.dir/src/main.c.o'
if main not in link:
    raise SystemExit('The game main object was not found in the link command.')
with tempfile.TemporaryDirectory(prefix='ssx-math-') as folder:
    temp = Path(folder)
    obj = temp/'probe.o'
    exe = temp/'probe'
    subprocess.run([link[0], '-I'+str(root/'src/recomp/gen'), '-c', str(root/'tools/math_probe.c'), '-o', str(obj)], check=True)
    link[link.index(main)] = str(obj)
    link[link.index('-o')+1] = str(exe)
    subprocess.run(link, cwd=build, check=True)
    memory = bytearray(0x800000)
    xbe = (root/'game/default.xbe').read_bytes()
    for section in json.loads((root/'game/ssx_analysis.json').read_text())['sections']:
        va, offset, size = int(section['virtual_addr'],16), int(section['raw_addr'],16), section['raw_size']
        if va+size > len(memory) or offset+size > len(xbe):
            raise SystemExit('Section outside the diagnostic memory image.')
        memory[va:va+size] = xbe[offset:offset+size]
    image = temp/'memory.bin'
    image.write_bytes(memory)
    subprocess.run([str(exe), str(image)], check=True)
