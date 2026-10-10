#!/usr/bin/env python3
"""Archive a verified local Windows bundle, without adding assets or saves."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    bundle, output = args.bundle.resolve(), args.output.resolve()
    if output.exists() or bundle == output or bundle in output.parents:
        parser.error('Use a new archive path outside the bundle.')
    manifest = json.loads((bundle / 'manifest.json').read_text())
    payload = manifest['files']
    actual = {str(p.relative_to(bundle)) for p in bundle.rglob('*') if p.is_file()}
    if actual != set(payload) | {'manifest.json'}:
        parser.error('Bundle contains files outside its manifest.')
    for name in actual:
        path = bundle / name
        if path.is_symlink() or not path.resolve().is_relative_to(bundle):
            parser.error(f'Unsafe payload path: {name}')
    for name, digest in payload.items():
        path = bundle / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            parser.error(f'Changed payload: {name}')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(actual):
            archive.write(bundle / name, 'ssx-tricky-windows/' + name)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise RuntimeError('Archive CRC verification failed')
        for name, digest in payload.items():
            if hashlib.sha256(archive.read('ssx-tricky-windows/' + name)).hexdigest() != digest:
                raise RuntimeError(f'Archive payload verification failed: {name}')
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_name(output.name + '.sha256').write_text(digest + '  ' + output.name + '\n')
    print(json.dumps({'archive': str(output), 'bytes': output.stat().st_size,
                      'sha256': digest, 'verified_payload_files': len(payload)}))


if __name__ == '__main__':
    main()
