"""Preserve stream availability publication on concurrently executing host threads."""
from pathlib import Path


def fix_stream_publication(text):
    marker = 'void sub_0014EA00(void)'
    if marker not in text:
        return text, False
    start = text.index(marker)
    end = text.index('\n/**', start)
    body = text[start:end]
    old = ('    RECOMP_STORE32(eax + 8, edx);\n'
           '    if (CMP_NE(_fa, _fb)) goto loc_0014EB0B;')
    published = ('loc_0014EB0B: ;\n'
                 '    RECOMP_STORE32(eax + 8, ecx);\n'
                 '    eax = RECOMP_READ32(esi + 0x60);')
    if published in body:
        if old in body:
            raise ValueError('Ambiguous stream publication hook')
        return text, True
    if body.count(old) != 1 or body.count('loc_0014EB0B: ;\n    eax = RECOMP_READ32(esi + 0x60);') != 1:
        raise ValueError('Unsupported translated stream producer')
    # Original 14EAFF publishes count before 14EB08 initializes the pointer.
    # The reader tests count before taking the producer's critical section.
    # Independent host threads can therefore consume its poison initializer.
    # ECX retains the exact new byte count after EDX becomes the packet pointer.
    body = body.replace(old, '    /* Publish packet pointer before its nonzero availability count. */\n'
                             '    if (CMP_NE(_fa, _fb)) goto loc_0014EB0B;', 1)
    body = body.replace('loc_0014EB0B: ;\n    eax = RECOMP_READ32(esi + 0x60);', published, 1)
    return text[:start] + body + text[end:], True


def apply(root):
    found = 0
    for path in (Path(root) / 'src/recomp/gen').glob('recomp_*.c'):
        old = path.read_text()
        new, matched = fix_stream_publication(old)
        found += matched
        if new != old:
            path.write_text(new)
    if found != 1:
        raise ValueError(f'Expected one supported stream producer, found {found}')
