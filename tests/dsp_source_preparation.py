"""Reject altered DSP inputs without networking or changing the real cache."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='ssx-dsp-inputs-') as work:
    fixture = Path(work)
    script = fixture / 'scripts/probe-dsp-core.py'
    script.parent.mkdir()
    shutil.copy2(root / 'scripts/probe-dsp-core.py', script)
    manifest = fixture / 'tools/dsp-research/source-hashes.json'
    manifest.parent.mkdir(parents=True)
    shutil.copy2(root / 'tools/dsp-research/source-hashes.json', manifest)
    source = fixture / 'build/dsp-research/hw/xbox/mcpx/apu/dsp/debug.h'
    source.parent.mkdir(parents=True)
    source.write_bytes(b'altered cached source')
    # Any accidental download is a test failure, including later missing files.
    runner = (
        'import io,runpy,sys,urllib.request\n'
        'def no_network(*a,**k): raise RuntimeError("Unexpected network access")\n'
        'urllib.request.urlopen=no_network\n'
        f'sys.argv={json.dumps([str(script),"--backend","--prepare-only","--fetch-missing"])}\n'
        f'runpy.run_path({str(script)!r},run_name="__main__")\n')
    result = subprocess.run([sys.executable,'-c',runner],capture_output=True,text=True)
    assert result.returncode == 2 and 'Upstream source hash mismatch: debug.h' in result.stderr
    assert source.read_bytes() == b'altered cached source'
    assert not (fixture / 'build/dsp-research/patched').exists()
    source.unlink()
    runner = runner.replace('urllib.request.urlopen=no_network',
        'urllib.request.urlopen=lambda *a,**k: io.BytesIO(b"altered downloaded source")')
    result = subprocess.run([sys.executable,'-c',runner],capture_output=True,text=True)
    assert result.returncode == 2 and 'Downloaded source hash mismatch: debug.h' in result.stderr
    assert not source.exists(), 'Unverified download was written to the source cache'
    assert not (fixture / 'build/dsp-research/patched').exists()
print('Altered cached/downloaded DSP inputs rejected before preparation; no network used')
