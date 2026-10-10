#!/usr/bin/env python3
"""Stage a local Linux bundle inside the Debian builder; no game assets copied.
The host supplies glibc, C++ runtime and graphics drivers. Validate the staged
launcher on the host before treating the bundle as a release candidate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.output.exists():
    parser.error('Output already exists; refusing to overwrite a staged bundle')
if not args.binary.is_file():
    parser.error('Build the executable first')
# Use the builder's library graph, not a mixture of host SDK installations.
if not shutil.which('dpkg-query'):
    parser.error('Run this script inside the Debian build container')
# Inspect the executable itself; a neighboring CMake cache may describe a
# different build, and staged/frozen executables need not have a cache.
symbols = subprocess.check_output(['nm', '--defined-only', str(args.binary)], text=True)
if not symbols.strip():
    parser.error('Use the unstripped build executable so compiled features can be identified')
dsp_enabled = any(line.split()[-1:] == ['ssx_apu_install_dsp_backend']
                  for line in symbols.splitlines())
dsp_default = dsp_enabled and any(line.split()[-1:] == ['ssx_apu_default_dsp']
                                 for line in symbols.splitlines())
dsp_manifest = None
if dsp_enabled:
    dsp_support = ROOT / 'tools/dsp-research'
    dsp_manifest = json.loads((dsp_support / 'source-hashes.json').read_text())
    dsp_original = ROOT / 'build/dsp-research/hw/xbox/mcpx/apu/dsp'
    for name, expected in dsp_manifest['source_hashes'].items():
        source = dsp_original / name
        if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            parser.error('Missing or changed pinned DSP source: ' + name)
linked = subprocess.check_output(['ldd', str(args.binary.resolve())], text=True)
if 'not found' in linked:
    parser.error('A required runtime library is missing: ' + linked)
provided = {'libc.so.6', 'libm.so.6', 'libpthread.so.0', 'librt.so.1',
            'libdl.so.2', 'libutil.so.1', 'libresolv.so.2', 'libanl.so.1',
            'libgcc_s.so.1', 'libstdc++.so.6', 'libEGL.so.1', 'libGL.so.1',
            'libGLX.so.0', 'libGLdispatch.so.0', 'libgbm.so.1'}
libs = {}
for line in linked.splitlines():
    match = re.match(r'\s*(\S+) => (/\S+) \(', line)
    if match:
        name, path = match.groups()
        libs[name] = Path(path).resolve()
out = args.output.resolve()
(out / 'lib').mkdir(parents=True)
(out / 'scripts').mkdir()
(out / 'licenses').mkdir()
shutil.copy2(args.binary, out / 'ssx_tricky')
shutil.copy2(ROOT / 'scripts/play-linux.py', out / 'scripts/play-linux.py')
shutil.copy2(ROOT / 'scripts/controller-mappings.txt', out / 'scripts/controller-mappings.txt')
manifest = {'status': 'experimental; full gameplay validation pending',
            'binary_sha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
            'experimental_dsp_compiled': dsp_enabled,
            'dsp_enabled_by_default': dsp_default,
            'libraries': [], 'host_libraries': [], 'patches': []}
if dsp_enabled:
    # Preserve untouched pinned inputs, adaptation patches, shims and the
    # integration sources. This is a component-source record for local testing,
    # not a claim that the bundle is a complete public source distribution.
    source_out = out / 'dsp-source'
    for name in dsp_manifest['source_hashes']:
        target = source_out / 'upstream' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(dsp_original / name, target)
    shutil.copytree(dsp_support, source_out / 'support')
    (source_out / 'integration').mkdir()
    for source in sorted((ROOT / 'src').glob('apu_dsp_*')):
        shutil.copy2(source, source_out / 'integration' / source.name)
    for name in ('audio_bridge.c', 'audio_service_trace.c'):
        shutil.copy2(ROOT / 'src' / name, source_out / 'integration' / name)
    shutil.copy2(ROOT / 'scripts/probe-dsp-core.py', source_out / 'probe-dsp-core.py')
    shutil.copy2(ROOT / 'scripts/audio_service_observation.py', source_out / 'integration/audio_service_observation.py')
    shutil.copy2(ROOT / 'CMakeLists.txt', source_out / 'CMakeLists.txt')
    shutil.copy2(ROOT / 'patches/0094-optional-apu-dsp-backend-hooks.patch', source_out)
    (source_out / 'README.txt').write_text(
        'Pinned xemu DSP interpreter: ' + dsp_manifest['revision'] + '\n'
        'Upstream: https://github.com/xemu-project/xemu\n'
        'Original per-file copyright/license notices are retained in upstream/.\n'
        'Core: GPL-2.0-or-later. DMA files: LGPL-2.0-or-later.\n'
        'License texts: ../licenses/common/GPL-2 and LGPL-2.\n'
        'Local changes: support/patches, support/shim and integration/.\n'
        'The workspace probe script verifies pinned hashes before applying patches.\n'
        'This component record requires the original project workspace to build.\n'
        'No original game assets, generated game code or saves are included.\n')
    manifest['dsp'] = {**dsp_manifest,
        'runtime_selection': ('Real DSP by default; SSX_APU_DSP=0 selects diagnostic downmix'
                              if dsp_default else 'SSX_APU_DSP=1; otherwise use the existing downmix'),
        'component_files': {str(p.relative_to(source_out)): hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in sorted(source_out.rglob('*')) if p.is_file()}}
for name, path in sorted(libs.items()):
    if name in provided or name.startswith('libdrm'):
        manifest['host_libraries'].append(name)
        continue
    shutil.copy2(path, out / 'lib' / name)
    owner = subprocess.run(['dpkg-query', '-S', str(path)], capture_output=True, text=True)
    if owner.returncode != 0:
        # Debian usrmerge can resolve /lib paths to /usr/lib even when the
        # package database still records the original spelling.
        owner = subprocess.run(['dpkg-query', '-S', '*/' + path.name], capture_output=True, text=True)
    package = owner.stdout.split(': ', 1)[0] if owner.returncode == 0 else None
    item = {'name': name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'package': package}
    if package:
        item['version'] = subprocess.check_output(
            ['dpkg-query', '-W', '-f=${Version}', package], text=True)
        copyright_path = Path('/usr/share/doc') / package.split(':')[0] / 'copyright'
        if copyright_path.is_file():
            shutil.copy2(copyright_path, out / 'licenses' / (package.replace(':', '_') + '.txt'))
    manifest['libraries'].append(item)
for name in ('xboxrecomp', 'nv2a_vsh_cpu'):
    shutil.copy2(ROOT / 'third_party' / name / 'LICENSE', out / 'licenses' / (name + '.txt'))
# The toolkit's top-level MIT notice does not replace its extracted APU/NV2A
# files' own notices. Preserve those headers alongside the package notices.
component_notices = []
for source in sorted((ROOT / 'third_party/xboxrecomp/src').rglob('*')):
    if source.suffix not in ('.c', '.h'):
        continue
    text = source.read_text()
    header = re.match(r'\s*/\*.*?\*/', text, re.DOTALL)
    if header and 'GNU Lesser General Public' in header.group():
        component_notices.append(str(source.relative_to(ROOT)) + '\n' + header.group().strip())
(out / 'licenses/xboxrecomp-component-notices.txt').write_text(
    '\n\n'.join(component_notices) + '\n')
# Debian copyright files refer to these shared license texts. Include the
# texts themselves so the notices remain readable outside the builder.
common = Path('/usr/share/common-licenses')
if not (common / 'LGPL-2').is_file():
    parser.error('The Debian builder is missing its common license texts')
shutil.copytree(common, out / 'licenses/common')
for patch in sorted((ROOT / 'patches').glob('*.patch')):
    manifest['patches'].append({'name': patch.name,
        'sha256': hashlib.sha256(patch.read_bytes()).hexdigest()})
manifest['build_packages'] = subprocess.check_output(
    ['dpkg-query', '-W', '-f=${binary:Package}\t${Version}\n'], text=True).splitlines()
manifest['source_hashes'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted((ROOT / 'src').glob('*')) if p.is_file()}
(out / 'play').write_text('''#!/bin/sh
set -eu
bundle_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$bundle_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec python3 "$bundle_root/scripts/play-linux.py" --binary "$bundle_root/ssx_tricky" "$@"
''')
(out / 'play').chmod(0o755)
(out / 'README.txt').write_text('''SSX Tricky — experimental Linux native port

Run: ./play --game-dir /path/to/extracted/game --save-dir /path/to/saves
The supported original Xbox US retail game files are required and are not
included. The launcher verifies default.xbe. Keep an independent save backup
while this experimental port is being validated.

Requires x86-64 Linux, Python 3.9 or newer, glibc 2.36 or newer, a C++ runtime at least as
new as Debian 12's, and host EGL/OpenGL graphics drivers. This bundle includes
its other linked runtime libraries. Run from a writable directory; the
launcher may create a process-local graphics-loader configuration.

Controls: Enter Start; Space confirm/jump; Escape back; Backspace reset rider;
arrow keys D-pad/menu; W build speed, S brake, A/D steer. Hold Space to crouch,
release to jump; Shift or Escape boost on the ground; R/Q/E grabs.
Default controls: hold Space with an arrow key to charge a rotation. The arrows
also control late rotations; C is the modifier for late rotations with W/A/S/D.
Pro controls use W/A/S/D to charge rotations while crouched and C for the combat
camera. Choose Options -> Configure Controller and confirm Done; Back cancels
unaccepted changes. Release rotation/grab inputs before landing. I/J/K/L push
opponents. With Tricky active, jump, hold a grab, then Shift/Escape for an Uber.
SDL gamepads are supported. --skip-intro skips the opening movies.

This is a local validation bundle, not a completed release. Course/mode
coverage and performance remain under active testing. Windows is unverified.
Runtime package versions/hashes and toolkit patch hashes are in manifest.json.
Dependency notices are in licenses/, with referenced shared texts in
licenses/common/. Component notices retain the APU/NV2A source headers.
The project source and build workflow
remain in the original workspace that produced this bundle.
''')
if dsp_enabled:
    with (out / 'README.txt').open('a') as readme:
        readme.write('\nThis build includes the real GP/EP DSP interpreter.\n')
        if dsp_default:
            readme.write('It runs by default; SSX_APU_DSP=0 selects diagnostic downmix.\n')
        else:
            readme.write('Enable it with: SSX_APU_DSP=1 ./play [the arguments above]\n')
        readme.write('Pinned component sources and local changes are in dsp-source/.\n')
manifest['runtime_files'] = {name: hashlib.sha256((out / name).read_bytes()).hexdigest()
    for name in ('play', 'scripts/play-linux.py', 'scripts/controller-mappings.txt')}
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'Staged {out}: {len(manifest["libraries"])} bundled runtime libraries')
