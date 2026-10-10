#!/usr/bin/env python3
"""Reproduce extraction, analysis, compilation, and a bounded startup probe."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TOOLKIT = ROOT / 'third_party/xboxrecomp'
REPORTS = ROOT / 'reports'
PINS = {
    'nv2a_vsh_cpu': ('https://github.com/abaire/nv2a_vsh_cpu.git', '1115255708c10c4841b65dcd2223262e7a316598'),
    'xboxrecomp': ('https://github.com/sp00nznet/xboxrecomp.git', '6f55eaa29d369860c950f442649e23ca86a3c275'),
    'extract-xiso': ('https://github.com/XboxDev/extract-xiso.git', '3f5b62cfe68f000b0e3c8a30104973f3a297948e'),
}
XBE_HASH = '0128bbf6f86cc27307156f9206245115a9950541317d05b7037cab6ef063c84a'

def run(args, log=None, cwd=ROOT, timeout=None):
    print('+', ' '.join(map(str, args)), flush=True)
    if log:
        REPORTS.mkdir(exist_ok=True)
        with (REPORTS / log).open('w') as out:
            return subprocess.run(list(map(str, args)), cwd=cwd, stdout=out,
                                  stderr=subprocess.STDOUT, check=True, timeout=timeout)
    return subprocess.run(list(map(str, args)), cwd=cwd, check=True, timeout=timeout)

def verify():
    xbe = ROOT / 'game/default.xbe'
    if hashlib.sha256(xbe.read_bytes()).hexdigest() != XBE_HASH:
        raise SystemExit('Unsupported XBE hash: this project currently targets one US retail build.')
    return xbe

def bootstrap():
    for name, (url, commit) in PINS.items():
        dest = ROOT / 'third_party' / name
        if not dest.exists():
            run(['git', 'clone', url, dest])
            run(['git', '-C', dest, 'checkout', '--detach', commit])
        actual = subprocess.check_output(['git', '-C', str(dest), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != commit:
            raise SystemExit(f'{name}: expected {commit}, found {actual}; resolve manually.')

    apply_patch_series()
    run([sys.executable, ROOT / 'scripts/probe-dsp-core.py',
         '--backend', '--prepare-only', '--fetch-missing'])

def apply_patch_series():
    # Later patches can modify earlier patches' context, so checking each patch
    # backwards against the final tree is not an idempotence test. Reconstruct
    # the series from the pinned revision, accepting only known file versions.
    patches = sorted((ROOT / 'patches').glob('*.patch'))
    paths = sorted({line[6:] for patch in patches for line in patch.read_text().splitlines()
                    if line.startswith('+++ b/')})
    if any(Path(p).is_absolute() or '..' in Path(p).parts for p in paths):
        raise SystemExit('Invalid patch path')
    versions = {p: set() for p in paths}
    with tempfile.TemporaryDirectory(prefix='ssx-patches-') as folder:
        staging = Path(folder)
        for p in paths:
            original = subprocess.run(['git', '-C', str(TOOLKIT), 'show', f'HEAD:{p}'],
                                      stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            data = original.stdout if original.returncode == 0 else None
            versions[p].add(data)
            if data is not None:
                (staging / p).parent.mkdir(parents=True, exist_ok=True)
                (staging / p).write_bytes(data)
        for patch in patches:
            run(['git', 'apply', patch], cwd=staging)
            for p in paths:
                path = staging / p
                versions[p].add(path.read_bytes() if path.exists() else None)
        # Validate everything before changing any source file.
        for p in paths:
            path = TOOLKIT / p
            current = path.read_bytes() if path.exists() else None
            if current not in versions[p]:
                raise SystemExit(f'Unrecognized local changes in {path}; refusing to overwrite.')
        for p in paths:
            expected = (staging / p).read_bytes()
            path = TOOLKIT / p
            if not path.exists() or path.read_bytes() != expected:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(expected)

def extract(iso):
    if (ROOT / 'game').exists():
        raise SystemExit('game/ already exists; refusing to overwrite extracted input.')
    run(['cmake', '-S', 'third_party/extract-xiso', '-B', 'build/extractor', '-G', 'Ninja',
         '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'])
    run(['cmake', '--build', 'build/extractor'])
    run([ROOT / 'build/extractor/extract-xiso', '-x', iso.resolve(), '-d', ROOT / 'game'], 'extract.log')
    verify()

def apply_game_hooks():
    # Keep generated game adaptations reproducible.
    marker = 'void sub_00148E00(void)\n{\n'
    hook = ('    extern int ssx_skip_startup_movie(volatile uint8_t *, const char *);\n'
            '    if (ssx_skip_startup_movie(&MEM8(ecx), (const char *)&MEM8(MEM32(esp + 4)))) {\n'
            '        esp += 16; return; /* original thiscall RET 12 */\n'
            '    }\n')
    found = 0
    for path in (ROOT / 'src/recomp/gen').glob('recomp_*.c'):
        text = path.read_text()
        if marker not in text:
            continue
        found += 1
        if marker + hook not in text:
            path.write_text(text.replace(marker, marker + hook, 1))
    if found != 1:
        raise SystemExit(f'Expected one movie entry for the supported XBE, found {found}')
    from stream_publication import apply
    apply(ROOT)
    from audio_service_observation import apply as observe_audio_service
    observe_audio_service(ROOT)

def apply_verified_function_ranges():
    """Restore audited enclosing routines without splitting their local labels."""
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    path = TOOLKIT / 'tools/disasm/output/functions.json'
    functions = json.loads(path.read_text())
    by_start = {int(f['start'], 16): f for f in functions}
    sections = json.loads((ROOT / 'game/ssx_analysis.json').read_text())['sections']
    image = (ROOT / 'game/default.xbe').read_bytes()
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    for item in json.loads((ROOT / 'scripts/verified-function-ranges.json').read_text()):
        start, end = int(item['start'], 16), int(item['end'], 16)
        if start not in by_start or not start < end:
            raise SystemExit('Invalid verified function range: ' + str(item))
        for other, info in by_start.items():
            if start < other < end and (info.get('has_prologue') or info.get('called_by')):
                raise SystemExit(f'Verified range crosses independently detected entry {other:#x}')
        section = next((sec for sec in sections if 'X' in sec['flags'] and
            int(sec['virtual_addr'],16) <= start < end <= int(sec['virtual_addr'],16)+sec['raw_size']), None)
        if section is None:
            raise SystemExit('Verified range is outside executable image bytes')
        offset = int(section['raw_addr'],16)+start-int(section['virtual_addr'],16)
        instructions = list(decoder.disasm(image[offset:offset+end-start],start))
        if not instructions or instructions[-1].address+instructions[-1].size != end:
            raise SystemExit('Verified range does not decode through its exact end')
        by_start[start].update(end=f'0x{end:08X}', size=end-start,
            num_instructions=len(instructions), verified_range_reason=item['reason'])
    path.write_text(json.dumps(functions, indent=2)+'\n')

def analyze():
    xbe = verify()
    # Leave identical generated files at their old timestamps so an added
    # entry point does not unnecessarily rebuild every unaffected code chunk.
    generated = ROOT / 'src/recomp/gen'
    previous = {p: (hashlib.sha256(p.read_bytes()).digest(), p.stat().st_atime_ns,
                    p.stat().st_mtime_ns) for p in generated.glob('*') if p.is_file()}
    commands = [
        ('xbe', ['tools.xbe_parser', xbe, '--json', ROOT / 'game/ssx_analysis.json']),
        ('disasm', ['tools.disasm', xbe, '--force', '--seed-functions', ROOT / 'scripts/function-seeds.json',
                    '--verified-seed-functions', ROOT / 'scripts/verified-function-seeds.json']),
        ('func-id', ['tools.func_id', xbe]),
        ('abi', ['tools.abi_analysis', xbe]),
        ('recomp', ['tools.recomp', xbe, '--all', '--game-name', 'SSX Tricky', '--split', '300',
                    '--manual-functions', ROOT / 'scripts/manual-functions.json',
                    '--gen-dir', ROOT / 'src/recomp/gen']),
    ]
    for name, args in commands:
        run([sys.executable, '-u', '-m', *args], name + '.log', TOOLKIT)
        if name == 'disasm':
            apply_verified_function_ranges()
            found = {int(f['start'], 16) for f in json.loads(
                (TOOLKIT / 'tools/disasm/output/functions.json').read_text())}
            for seed_file in ['function-seeds.json', 'verified-function-seeds.json']:
                for entry in json.loads((ROOT / 'scripts' / seed_file).read_text()):
                    if int(entry['start'], 16) not in found:
                        raise SystemExit(f"Required measured entry {entry['start']} was not recovered")
    apply_game_hooks()
    for path, (digest, access_ns, modified_ns) in previous.items():
        if path.is_file() and hashlib.sha256(path.read_bytes()).digest() == digest:
            os.utime(path, ns=(access_ns, modified_ns))
    for folder in ['disasm', 'recomp']:
        source = TOOLKIT / 'tools' / folder / 'output/summary.json'
        (REPORTS / (folder + '-summary.json')).write_text(source.read_text())

def build():
    apply_game_hooks()
    run(['cmake', '-S', '.', '-B', 'build/linux', '-G', 'Ninja'], 'configure.log')
    run(['cmake', '--build', 'build/linux', '-j', '4'], 'game-build.log')

def probe(gpu_exec=False, save_dir=None, seconds=10, window=False, skip_intro=False):
    verify()
    # No core dumps; a failed startup should not fill the disk with the guest mapping.
    import resource
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if seconds < 1:
        raise SystemExit('--seconds must be positive')
    os.environ['RECOMP_WATCHDOG_SECS'] = str(seconds)
    if skip_intro:
        os.environ['SSX_SKIP_INTRO'] = '1'
        print('Skipping the two startup movies (experimental menu bring-up mode).', flush=True)
    if window:
        os.environ['RECOMP_FB_WINDOW'] = '1'
        os.environ['RECOMP_FB_CAPTURE'] = str(REPORTS / 'framebuffer.bmp')
    if gpu_exec:
        os.environ['RECOMP_PB_EXEC'] = '1'
        os.environ['RECOMP_VBLANK'] = '1'
        print('Experimental GPU execution and approximate vblank enabled.', flush=True)
    save_dir = save_dir or ROOT / 'saves'
    try:
        run([ROOT / 'build/linux/ssx_tricky', ROOT / 'game', save_dir], 'startup.log', timeout=seconds + 10)
    except subprocess.CalledProcessError as error:
        raise SystemExit(f'Startup failed with return code {error.returncode}; inspect reports/startup.log.')
    except subprocess.TimeoutExpired:
        raise SystemExit(f'Startup timed out after {seconds + 10} seconds; inspect reports/startup.log.')

    log = (REPORTS / 'startup.log').read_text()
    if 'HalReturnToFirmware' in log:
        raise SystemExit('Guest requested firmware return before reaching a menu; inspect reports/startup.log.')
    print('Probe completed; exit status alone does not establish playable gameplay.')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=['bootstrap', 'extract', 'analyze', 'build', 'probe'])
    parser.add_argument('--iso', type=Path, default=ROOT / 'iso/SSX Tricky (USA).xiso.iso')
    parser.add_argument('--gpu-exec', action='store_true', help='Probe using experimental GPU interpreter and approximate vblank')
    parser.add_argument('--save-dir', type=Path, help='Separate save/cache directory for the probe')
    parser.add_argument('--seconds', type=int, default=10, help='Probe watchdog duration (default: 10)')
    parser.add_argument('--window', action='store_true', help='Show guest framebuffer and capture reports/framebuffer.bmp')
    parser.add_argument('--skip-intro', action='store_true', help='Opt in to skipping only the EA and SSX startup movies')
    args = parser.parse_args()
    if args.stage == 'extract':
        extract(args.iso)
    elif args.stage == 'probe':
        probe(args.gpu_exec, args.save_dir, args.seconds, args.window, args.skip_intro)
    else:
        globals()[args.stage]()

if __name__ == '__main__':
    main()
