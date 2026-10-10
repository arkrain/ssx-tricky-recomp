#!/usr/bin/env python3
"""Stage a local Win64 test bundle inside the Windows builder, without game data."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SYSTEM_DLLS = set('advapi32.dll bcrypt.dll d3d11.dll d3dcompiler_47.dll gdi32.dll imm32.dll '
                 'kernel32.dll mfplat.dll mfreadwrite.dll msvcrt.dll ole32.dll '
                 'oleaut32.dll opengl32.dll setupapi.dll shell32.dll user32.dll '
                 'version.dll winmm.dll xaudio2_8.dll xinput1_4.dll'.split())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, default=Path('/opt/windows-prefix'))
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output already exists; use a new staging directory.')
    if not args.binary.is_file():
        parser.error('Build the executable first.')
    symbols = subprocess.check_output(
        ['x86_64-w64-mingw32-nm', '--defined-only', str(args.binary)], text=True)
    dsp_enabled = any(line.split()[-1:] == ['ssx_apu_install_dsp_backend']
                      for line in symbols.splitlines())
    dsp_support = ROOT / 'tools/dsp-research'
    dsp_original = ROOT / 'build/dsp-research/hw/xbox/mcpx/apu/dsp'
    dsp_record = None
    if dsp_enabled:
        dsp_record = json.loads((dsp_support / 'source-hashes.json').read_text())
        for name, expected in dsp_record['source_hashes'].items():
            source = dsp_original / name
            if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
                parser.error('Missing or changed pinned DSP source: ' + name)
    candidates = {p.name.lower(): p for p in (args.prefix / 'bin').glob('*.dll')}
    pthread = Path(subprocess.check_output(
        ['x86_64-w64-mingw32-gcc-posix', '-print-file-name=libwinpthread-1.dll'],
        text=True).strip())
    candidates[pthread.name.lower()] = pthread
    pending = [args.binary]
    payload = {args.binary.name: args.binary}
    imports = {}
    system = set()
    while pending:
        binary = pending.pop()
        info = subprocess.check_output(['x86_64-w64-mingw32-objdump', '-p', str(binary)], text=True)
        if 'file format pei-x86-64' not in info:
            parser.error(f'Expected a Win64 PE binary: {binary}')
        names = re.findall(r'DLL Name: (\S+)', info)
        imports[binary.name] = names
        for name in names:
            lower = name.lower()
            if lower in SYSTEM_DLLS:
                system.add(name)
                continue
            dependency = candidates.get(lower)
            if dependency is None or not dependency.is_file():
                parser.error(f'Missing or unrecognized dependency {name} of {binary.name}')
            if dependency.name not in payload:
                payload[dependency.name] = dependency
                pending.append(dependency)
    out = args.output
    out.mkdir(parents=True)
    for name, source in payload.items():
        shutil.copy2(source, out / name)
    shutil.copytree(args.prefix / 'licenses', out / 'licenses')
    shutil.copy2('/usr/share/doc/mingw-w64-common/copyright', out / 'licenses/mingw-w64.txt')
    shutil.copy2(ROOT / 'third_party/xboxrecomp/LICENSE', out / 'licenses/xboxrecomp.txt')
    shutil.copy2(ROOT / 'third_party/nv2a_vsh_cpu/LICENSE', out / 'licenses/nv2a_vsh_cpu.txt')
    if dsp_record:
        component = out / 'dsp-source'
        for name in dsp_record['source_hashes']:
            target = component / 'upstream' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(dsp_original / name, target)
        shutil.copytree(dsp_support, component / 'support',
                        ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
        (component / 'integration').mkdir()
        for source in sorted((ROOT / 'src').glob('apu_dsp_*')):
            shutil.copy2(source, component / 'integration' / source.name)
        shutil.copy2(ROOT / 'src/audio_bridge.c', component / 'integration/audio_bridge.c')
        shutil.copy2(ROOT / 'CMakeLists.txt', component / 'CMakeLists.txt')
        shutil.copy2(ROOT / 'scripts/probe-dsp-core.py', component / 'probe-dsp-core.py')
        shutil.copy2(ROOT / 'patches/0094-optional-apu-dsp-backend-hooks.patch', component)
        for name in ('GPL-2', 'LGPL-2.1'):
            shutil.copy2(Path('/usr/share/common-licenses') / name, out / 'licenses' / name)
        (component / 'README.txt').write_text(
            'Pinned xemu DSP interpreter: ' + dsp_record['revision'] + '\n'
            'Upstream: https://github.com/xemu-project/xemu\n'
            'Original source notices retained. Core GPL-2.0-or-later; DMA LGPL-2.0-or-later.\n'
            'Local adaptations: support/patches, support/shim and integration/.\n'
            'This component record requires the original project workspace to rebuild.\n'
            'No game assets, generated game code or saves are included.\n')
    for name in ('start-game.cmd', 'start-game.ps1'):
        shutil.copy2(ROOT / 'packaging' / name, out / name)
    (out / 'README.txt').write_text(
        'Experimental SSX Tricky Win64 port, locally staged for testing.\n'
        'Requires Windows 10 version1903 or newer / Windows 11 x64 and an OpenGL 3.3 driver.\n'
        'Place your original Xbox US retail extracted assets in game/.\n'
        'Double-click start-game.cmd; saves are kept separately in saves/.\n'
        'PowerShell: ./start-game.ps1 -GameDir "C:\\My Games\\SSX" -SaveDir "C:\\SSX Saves"\n'
        'Options: -SkipIntro, -Software (slow), -NoAudio.\n'
        'Arrows: menu; Enter: Start; Space: A; Escape: B; Shift: X; R: Y.\n'
        'W/A/S/D: left stick; I/J/K/L: right stick; Q/E: triggers; C/V: white/black.\n'
        'Wine gameplay is verified; native Windows hardware validation remains open.\n'
        'Original assets, saves and generated game sources are not included.\n'
        'This local bundle is not a complete public source distribution.\n'
        'Keep the project and dependency sources for rebuilding; do not publish this as a release.\n')
    manifest = {
        'status': 'experimental local test bundle; native Windows validation pending',
        'experimental_dsp_compiled': dsp_enabled, 'dsp': dsp_record,
        'imports': imports, 'system_dlls': sorted(system),
        'files': {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in sorted(out.rglob('*')) if p.is_file()},
    }
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Staged {len(payload)} PE files in {out}')


if __name__ == '__main__':
    main()
