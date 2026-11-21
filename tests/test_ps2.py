import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ps2 import costume, members, unpack


def archive(entries):
    start=16+16*len(entries)
    header=b'BAR\1'+struct.pack('<I',len(entries))+bytes(8)
    rows=bytearray()
    data=bytearray()
    for kind,part in entries:
        rows.extend(struct.pack('<I4sII',kind,b'test',start+len(data),len(part)))
        data.extend(part)
    return header+rows+data


class DonorTests(unittest.TestCase):
    def test_costume_preserves_pc_members(self):
        pc=archive([(4,b'old'),(7,b'abc'),(23,b'collision')])
        merged=costume(pc,archive([(4,b'new mesh'),(7,b'xy')]))
        self.assertIn(b'collision',merged)
        for kind, expected in [(4,b'new mesh'),(7,b'xy')]:
            _,off,length=members(merged)[kind]
            self.assertEqual(merged[off:off+length],expected)

    def test_unpack(self):
        literal=b'abc'+struct.pack('>I',3)+b'\xff'+bytes(5)
        self.assertEqual(unpack(literal,3),b'abc')
        with self.assertRaises(ValueError):
            unpack(literal,10)
