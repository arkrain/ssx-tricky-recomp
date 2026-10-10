#!/usr/bin/env python3
"""Launch the current Linux port with its required device backends enabled."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex

ROOT = Path(__file__).resolve().parents[1]


def matching_nvidia_libraries(env, cache):
    """Use an installed matching Flatpak driver when system libraries mismatch.

    This changes only the launched process, never the system driver or kernel.
    An explicit EGL vendor selection supplied by the caller takes precedence.
    """
    if env.get('__EGL_VENDOR_LIBRARY_FILENAMES'):
        return None
    try:
        version = re.search(r'Kernel Module.*?\b(\d+\.\d+\.\d+)\b',
                            Path('/proc/driver/nvidia/version').read_text()).group(1)
    except (OSError, AttributeError):
        return None
    for directory in ('/usr/lib64', '/usr/lib/x86_64-linux-gnu', '/usr/lib'):
        lib = Path(directory) / 'libEGL_nvidia.so.0'
        if lib.exists() and lib.resolve().name == 'libEGL_nvidia.so.' + version:
            return None
    runtime = 'org.freedesktop.Platform.GL.nvidia-' + version.replace('.', '-')
    roots = (Path('/var/lib/flatpak/runtime'), Path.home() / '.local/share/flatpak/runtime')
    for root in roots:
        for directory in sorted((root / runtime / platform.machine()).glob('*/active/files/lib')):
            library = directory / ('libEGL_nvidia.so.' + version)
            if not library.is_file():
                continue
            cache.mkdir(parents=True, exist_ok=True)
            vendor = cache / ('egl-nvidia-' + version + '.json')
            vendor.write_text(json.dumps({'file_format_version': '1.0.0',
                'ICD': {'library_path': str(library.resolve())}}) + '\n')
            env['__EGL_VENDOR_LIBRARY_FILENAMES'] = str(vendor.resolve())
            env['LD_LIBRARY_PATH'] = str(directory.resolve()) + (
                ':' + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
            return version
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/linux-release/ssx_tricky')
    parser.add_argument('--game-dir', type=Path, default=ROOT / 'game')
    parser.add_argument('--save-dir', type=Path, default=ROOT / 'saves')
    parser.add_argument('--audio', action=argparse.BooleanOptionalAction, default=True,
                        help='Enable APU audio (default; --no-audio disables it)')
    parser.add_argument('--skip-intro', action='store_true', help='Skip the two startup movies')
    parser.add_argument('--software', action='store_true', help='Use the slow CPU rasterizer')
    parser.add_argument('--dry-run', action='store_true', help='Print the launch command')
    parser.add_argument('--anisotropy', type=int, choices=(1,4,8,16), default=1, help='Texture filtering enhancement (default: original)')
    parser.add_argument('--render-scale', type=int, choices=(1,2,3,4), default=1, help='Internal resolution multiplier: 2=1280x960, 3=1920x1440, 4=2560x1920')
    args = parser.parse_args()
    if args.software and (args.render_scale!=1 or args.anisotropy!=1):
        parser.error('Graphics enhancements require OpenGL')
    if not args.binary.is_file():
        parser.error('Build the port first: cmake --build --preset linux-release')
    xbe = args.game_dir / 'default.xbe'
    try:
        digest = hashlib.sha256(xbe.read_bytes()).hexdigest()
    except OSError as error:
        parser.error(f'Cannot read game image {xbe}: {error}')
    if digest != '0128bbf6f86cc27307156f9206245115a9950541317d05b7037cab6ef063c84a':
        parser.error('Unsupported game image; this build requires the original Xbox US retail release.')
    env = os.environ.copy()
    mappings = Path(__file__).with_name("controller-mappings.txt")
    if mappings.is_file():
        env.setdefault("SDL_GAMECONTROLLERCONFIG_FILE", str(mappings.resolve()))
    for key in ('RECOMP_PB_EXEC', 'RECOMP_VBLANK', 'RECOMP_FB_WINDOW', 'RECOMP_USB'):
        env[key] = '1'
    env['RECOMP_RENDER_EVERY'] = '1'
    env['SSX_GL_ANISOTROPY'] = str(args.anisotropy)
    env['SSX_GL_RENDER_SCALE'] = str(args.render_scale)
    env['SSX_GL'] = '0' if args.software else '1'
    if args.audio:
        env['RECOMP_AC97_READY'] = '1'
    else:
        env.pop('RECOMP_AC97_READY', None)
    if args.skip_intro:
        env['SSX_SKIP_INTRO'] = '1'
    version = None if args.software else matching_nvidia_libraries(env, ROOT / 'build/runtime')
    if version:
        print(f'Using installed NVIDIA {version} libraries for this process.', flush=True)
    command = [str(args.binary.resolve()), str(args.game_dir.resolve()), str(args.save_dir.resolve())]
    if args.dry_run:
        removed = [arg for key in os.environ if key not in env for arg in ('-u', key)]
        changed = [key + '=' + value for key, value in env.items() if os.environ.get(key) != value]
        print(shlex.join(['env', *removed, *changed, *command]))
        return
    os.chdir(ROOT)
    os.execvpe(command[0], command, env)


if __name__ == '__main__':
    main()
