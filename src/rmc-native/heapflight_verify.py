"""Compare the actual host-compiled heap/flight plan against the port's edit recipes."""
from pathlib import Path
import ctypes
import json
import struct
import sys

lib, exe, recipes = map(Path, sys.argv[1:])
plan = ctypes.CDLL(str(lib.resolve())).check_plan
plan.argtypes = [ctypes.c_void_p, ctypes.c_int]
plan.restype = None
data = exe.read_bytes()
nt = struct.unpack_from('<I', data, 60)[0]
opt = nt + 24
preferred = struct.unpack_from('<Q', data, opt + 24)[0]
image_size = struct.unpack_from('<I', data, opt + 56)[0]
section = opt + struct.unpack_from('<H', data, nt + 20)[0]
pristine = bytearray(image_size)
for i in range(struct.unpack_from('<H', data, nt + 6)[0]):
    virtual, rva, n, offset = struct.unpack_from('<IIII', data, section + 40*i + 8)
    length = min(n, virtual)
    pristine[rva:rva+length] = data[offset:offset+length]

flight = json.loads((recipes / 'mod_neverland_edits.json').read_text())['edits']
hacks = json.loads((recipes / 'mod_moose_edits.json').read_text())['edits']
assert struct.unpack_from('<Q', pristine, 0x749740)[0] == preferred + 0x29f33c0
for mode in (0, 1, 2):
    image = bytearray(pristine)
    base = ctypes.addressof(ctypes.c_char.from_buffer(image))
    struct.pack_into('<Q', image, 0x749740, base + 0x29f33c0)
    expected = bytearray(image)
    for edit in flight + ([] if mode == 1 else hacks):
        at = int(edit['rva'], 16)
        old, new = bytes.fromhex(edit['old']), bytearray.fromhex(edit['new'])
        assert expected[at:at+len(old)] == old
        if at == 0x30661b:
            setter = struct.unpack_from('<Q', new, 15)[0] - preferred
            struct.pack_into('<Q', new, 15, base + setter)
        expected[at:at+len(new)] = new
    if mode != 1:
        for site in [0x39d719, 0x39dac1, 0x3e5f90, 0x3e5f3d]:
            assert expected[site+1] == 0x8d
            expected[site+1] = 0x8b
        struct.pack_into('<Q', expected, 0xbf33c0, 80 << 25)
        struct.pack_into('<Q', expected, 0x749740, (80 << 25) + (512 << 20))
        struct.pack_into('<Q', expected, 0x2a254c0, 96 << 25)
    if mode == 2:
        expected[0x92758:0x9275d] = bytes.fromhex('a9 00 00 00 00')
        expected[0x404d60:0x404d62] = bytes.fromhex('c3 90')
    plan(base, mode)
    if image != expected:
        at = next(i for i, pair in enumerate(zip(image, expected)) if pair[0] != pair[1])
        raise ValueError(f'mode {mode}, unexpected planned edit at {at:x}')
print('Heap/flight: default, disabled and optional plans agree throughout Steam image; predicates pass.')
