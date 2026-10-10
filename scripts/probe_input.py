"""Atomic writes for the opt-in RECOMP_TEST_INPUT_FILE controller probe."""
import os
from pathlib import Path


def write_input(path, mask, axes=None):
    if not isinstance(mask, int) or not 0 <= mask <= 0xffffff:
        raise ValueError('mask must be a 24-bit unsigned integer')
    values = None if axes is None else tuple(axes)
    if values is not None and (len(values) != 4 or any(
            not isinstance(x, int) or not -32768 <= x <= 32767 for x in values)):
        raise ValueError('axes must be four signed 16-bit integers')
    text = f'{mask:x}'
    if values is not None:
        text += ' ' + ' '.join(str(x) for x in values)
    path = Path(path)
    temporary = path.with_name(f'.{path.name}.{os.getpid()}.tmp')
    try:
        temporary.write_text(text + '\n', encoding='ascii')
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)
