"""Resolve ELF symbol addresses in a running Linux process (including PIE)."""
import os
from pathlib import Path
import struct
import subprocess


def process_symbols(pid, binary, names):
    binary = Path(binary).resolve()
    if not os.path.samefile(binary, f'/proc/{pid}/exe'):
        raise ValueError('The supplied binary is not the running executable')
    with binary.open('rb') as stream:
        header = stream.read(64)
        if header[:6] != b'\x7fELF\x02\x01':
            raise ValueError('Expected a little-endian ELF64 executable')
        elf_type = struct.unpack_from('<H', header, 16)[0]
        bias = 0
        if elf_type == 3:
            offset = struct.unpack_from('<Q', header, 32)[0]
            size, count = struct.unpack_from('<HH', header, 54)
            segments = []
            for index in range(count):
                stream.seek(offset + index * size)
                kind, flags, file_offset, virtual, *_ = struct.unpack('<IIQQQQQQ', stream.read(56))
                if kind == 1 and file_offset == 0:
                    segments.append(virtual)
            maps = Path(f'/proc/{pid}/maps').read_text().splitlines()
            # Containers can expose the same executable under a different path.
            # Check candidate paths through the process's mount namespace.
            # Some filesystems report a different stat device from maps.
            identity = binary.stat()
            starts = []
            for line in maps:
                parts = line.split(None, 5)
                if (len(parts) != 6 or int(parts[2], 16) != 0
                        or int(parts[4]) != identity.st_ino
                        or not parts[5].startswith('/')):
                    continue
                mapped_path = parts[5].replace('\\012', '\n')
                try:
                    same = os.path.samefile(binary, f'/proc/{pid}/root{mapped_path}')
                except OSError:
                    same = mapped_path == str(binary)
                if same:
                    starts.append(int(parts[0].split('-')[0], 16))
            if len(segments) != 1 or len(starts) != 1:
                raise ValueError('Cannot uniquely locate executable load mapping')
            bias = starts[0] - (segments[0] & ~(os.sysconf('SC_PAGE_SIZE') - 1))
        elif elf_type != 2:
            raise ValueError('Unsupported ELF executable type')
    symbols = {}
    for line in subprocess.check_output(['nm', str(binary)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] in names:
            symbols[fields[2]] = int(fields[0], 16) + bias
    if set(symbols) != set(names):
        raise ValueError('Required telemetry symbols were not found')
    return symbols
