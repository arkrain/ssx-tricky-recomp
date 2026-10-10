"""Run the actual generated producer and a forced pre-fix publication negative."""
import argparse
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
p.add_argument('--root', type=Path, required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
source = (a.root / 'src/recomp/gen/recomp_0018.c').read_text()
start = source.index('void sub_0014EA00(void)')
body = source[start:source.index('\n/**', start)]
late = 'loc_0014EB0B: ;\n    RECOMP_STORE32(eax + 8, ecx);'
assert body.count(late) == 1
negative = body.replace(late, 'loc_0014EB0B: ;').replace(
    '    if (CMP_NE(_fa, _fb)) goto loc_0014EB0B;',
    '    RECOMP_STORE32(eax + 8, edx);\n    if (CMP_NE(_fa, _fb)) goto loc_0014EB0B;')
for label, text, expected in [('actual', body, 0), ('negative', negative, 42)]:
    include = (a.work / (label + '.inc')).resolve()
    include.write_text(text)
    executable = (a.work / label).resolve()
    subprocess.run([a.cc, '-std=c11', '-O2', '-DPRODUCER_INCLUDE="' + str(include) + '"',
                    str(a.root / 'tests/stream_publication_fixture.c'), '-o', str(executable)], check=True)
    result = subprocess.run([str(executable)], capture_output=True, text=True)
    assert result.returncode == expected, (label, result.returncode, result.stdout, result.stderr)
    print(label + ': ' + (result.stdout or result.stderr).strip())
