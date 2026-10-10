#!/usr/bin/env python3
"""Asset-free serialization and atomicity checks for diagnostic controller input."""
import tempfile
import threading
from pathlib import Path
from probe_input import write_input

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'controller'
    write_input(path, 0x90100)
    assert path.read_text() == '90100\n'
    axes = [-32768, 32767, -12345, 23456]
    expected = '200 -32768 32767 -12345 23456\n'
    write_input(path, 0x200, axes)
    assert path.read_text() == expected
    for mask, values in [(-1, None), (0x1000000, None), (0, [1, 2, 3]),
                         (0, [1, 2, 3, 32768]), (0, [1, 2, 3, -32769]),
                         (0, [1, 2, 3, float('nan')])]:
        try:
            write_input(path, mask, values)
        except ValueError:
            pass
        else:
            raise AssertionError((mask, values))
        assert path.read_text() == expected
    stopped = threading.Event()
    observations, errors = [], []
    def reader():
        try:
            while not stopped.is_set():
                text = path.read_text()
                if text not in (expected, '0 0 0 0 0\n'):
                    errors.append(text)
                observations.append(1)
        except BaseException as error:
            errors.append(repr(error))
    thread = threading.Thread(target=reader)
    thread.start()
    try:
        for index in range(1000):
            write_input(path, 0 if index % 2 else 0x200,
                        [0, 0, 0, 0] if index % 2 else axes)
    finally:
        stopped.set()
        thread.join()
    assert observations and not errors, errors
    write_input(path, 0)
    assert path.read_text() == '0\n'
    assert list(Path(directory).iterdir()) == [path]
    print(f'Probe input checks passed: {len(observations)} complete concurrent reads')
