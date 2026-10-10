"""The last byte of SSX's XGRPH RET 0x20 lies in the virtual zero tail."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'third_party/xboxrecomp'))
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from tools.disasm.loader import BinaryImage, SectionInfo


def test_return_immediate_in_zero_tail():
    section = SectionInfo('XGRPH', 0x11000, 4, 0, 3, False, True, 'X')
    image = BinaryImage('synthetic', b'\xc9\xc2\x20\xff\xff', 0x10000,
                        0x2000, 0x11000, 0, [section])
    loaded = image.get_section_data(section)
    assert loaded == b'\xc9\xc2\x20\x00'
    instructions = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(loaded, 0x11000))
    assert [(i.mnemonic, i.op_str) for i in instructions] == [('leave', ''), ('ret', '0x20')]
    assert image.read_bytes_at_va(0x11002, 2) == b'\x20\x00'
    assert image.read_bytes_at_va(0x11003, 1) == b'\x00'
    assert image.read_bytes_at_va(0x11003, 2) is None
