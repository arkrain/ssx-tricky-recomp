#!/usr/bin/env python3
"""Archive a staged Linux bundle with an allowlist and manifest verification.

Requires the original source checkout for patch verification. Game assets,
saves and diagnostic captures are outside the archive allowlist. Archive
metadata and gzip timestamps are normalized for repeatability.
"""
import argparse
import gzip
import hashlib
import json
import os
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bundle', type=Path, required=True)
p.add_argument('--archive', type=Path, required=True)
p.add_argument('--report-prefix', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
bundle = a.bundle.resolve()
allowed = ('README.txt', 'play', 'manifest.json', 'ssx_tricky',
           'scripts/play-linux.py', 'lib', 'licenses', 'dsp-source')

def require(condition, message):
    if not condition:
        raise SystemExit(message)

def stream_digest(f):
    h = hashlib.sha256()
    for block in iter(lambda: f.read(1024 * 1024), b''):
        h.update(block)
    return h.hexdigest()

def digest(path):
    with path.open('rb') as f:
        return stream_digest(f)

def relative(name):
    path = PurePosixPath(name)
    require(not path.is_absolute() and '..' not in path.parts and bool(path.parts),
            f'Invalid manifest path: {name}')
    return str(path)

require(not a.archive.exists(), f'Archive already exists: {a.archive}')
m = json.loads((bundle / 'manifest.json').read_text())
files = {'ssx_tricky': m['binary_sha256']}
for name, expected in m.get('runtime_files', {}).items():
    name = relative(name)
    require(name in ('play', 'scripts/play-linux.py', 'scripts/controller-mappings.txt'),
            f'Unexpected runtime file: {name}')
    files[name] = expected
    if name not in allowed:
        allowed += (name,)
for lib in m['libraries']:
    name = relative(lib['name'])
    require('/' not in name, f'Invalid library name: {name}')
    files['lib/' + name] = lib['sha256']
for name, expected in m['dsp']['component_files'].items():
    files['dsp-source/' + relative(name)] = expected
for name, expected in files.items():
    require(digest(bundle / name) == expected, f'Bundle hash mismatch: {name}')
for patch in m['patches']:
    name = relative(patch['name'])
    require(digest(root / 'patches' / name) == patch['sha256'],
            f'Patch hash mismatch: {name}')

def normalized(member):
    require(member.isfile() or member.isdir(),
            f'Unexpected special file or link: {member.name}')
    member.uid = member.gid = member.mtime = 0
    member.uname = member.gname = ''
    return member

a.archive.parent.mkdir(parents=True, exist_ok=True)
a.report_prefix.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='ssx-archive-', dir=a.archive.parent) as work:
    temporary = Path(work) / 'bundle.tar.gz'
    with temporary.open('wb') as raw:
        with gzip.GzipFile(filename='', fileobj=raw, mode='wb', mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w') as tar:
                for name in allowed:
                    tar.add(bundle / name, arcname='ssx-tricky-linux/' + name,
                            filter=normalized)
    with tarfile.open(temporary) as tar:
        names = tar.getnames()
        require(len(names) == len(set(names)), 'Duplicate archive entries')
        for name in names:
            require(name.startswith('ssx-tricky-linux/'), f'Unexpected root: {name}')
            rel = name.removeprefix('ssx-tricky-linux/')
            require(any(rel == x or rel.startswith(x + '/') for x in allowed),
                    f'Entry outside allowlist: {name}')
        for name, expected in files.items():
            f = tar.extractfile('ssx-tricky-linux/' + name)
            require(f is not None, f'Missing archived file: {name}')
            with f:
                actual = stream_digest(f)
            require(actual == expected, f'Archived hash mismatch: {name}')
    result = {'dsp_default': m['dsp_enabled_by_default'], 'verified_files': len(files),
              'binary_sha256': m['binary_sha256'], 'patches': len(m['patches']),
              'libraries': len(m['libraries']), 'archive_bytes': temporary.stat().st_size,
              'entries': len(names), 'archive_sha256': digest(temporary),
              'normalized_metadata': True}
    # Publish only a completely verified archive; never overwrite an earlier build.
    os.link(temporary, a.archive)

prefix = str(a.report_prefix)
Path(prefix + '-archive-contents.json').write_text(json.dumps(names, indent=2) + '\n')
Path(prefix + '-verification.json').write_text(json.dumps(result, indent=2) + '\n')
Path(prefix + '-archive.sha256').write_text(result['archive_sha256'] + '  ' + str(a.archive) + '\n')
print(json.dumps(result, indent=2))
