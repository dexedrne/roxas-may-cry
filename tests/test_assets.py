"""Exercise package decoding and delta validation without game data."""
import hashlib
import lzma
from pathlib import Path
import sys
import struct
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from assets import Game, apply_delta
from kh2_package import HED, HD, decrypt_prefix, egs_mask
from text import reword




class BuilderTest(unittest.TestCase):
    def test_packages(self):
        original = bytes(range(256)) * 4
        name = 'obj/test.mset'
        pin = hashlib.sha256(original).hexdigest()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            hed = root / 'kh2_first.hed'
            for mode in (-2, -1, len(zlib.compress(original))):
                payload = zlib.compress(original) if mode >= 0 else original
                seed = struct.pack('<4i', len(original), 1, mode, 0)
                if mode != -2:
                    payload = decrypt_prefix(payload, egs_mask(seed))
                record = seed + bytes(HD.size) + payload
                hed.write_bytes(HED.pack(hashlib.md5(name.encode()).digest(), 0, len(record), len(original)))
                hed.with_suffix('.pkg').write_bytes(record)
                game = Game(root)
                self.assertEqual(game.read(name, pin), original)
                with self.assertRaises(ValueError):
                    game.read(name, 'incorrect')

    def test_named_hd_texture(self):
        name = 'menu/fm/title.2ld'
        texture = b'owned RGBA data' * 103
        packed = zlib.compress(texture)
        seed = struct.pack('<4i',0,1,-2,0)
        size = (len(packed)+15)&~15
        payload = decrypt_prefix(packed.ljust(size,b'\0'),egs_mask(seed))
        record = seed + HD.pack(b'-7.png',0,0,len(texture),len(packed)) + payload
        with tempfile.TemporaryDirectory() as tmp:
            hed=Path(tmp)/'kh2_first.hed'
            hed.write_bytes(HED.pack(hashlib.md5(name.encode()).digest(),0,len(record),0))
            hed.with_suffix('.pkg').write_bytes(record)
            game=Game(Path(tmp))
            self.assertEqual(game.read('remastered/'+name+'/-7.png',hashlib.sha256(texture).hexdigest()),texture)
            with self.assertRaises(ValueError):
                game.read('remastered/'+name+'/-8.png',hashlib.sha256(texture).hexdigest())

    def test_delta(self):
        with tempfile.TemporaryDirectory() as tmp:
            patch = Path(tmp) / 'delta'
            magic = bytes.fromhex('72730236')
            patch.write_bytes(lzma.compress(magic + bytes([69, 1, 3, 1, 33, 0])))
            self.assertEqual(apply_delta(b'01234', patch), b'123!')
            patch.write_bytes(lzma.compress(magic + bytes([69, 4, 3, 0])))
            with self.assertRaises(ValueError):
                apply_delta(b'01234', patch)

    def test_reword(self):
        first, second = bytes([0x2e, 0x9a, 0]), bytes([0x07, 1, 2, 3, 4, 0x9b, 0])
        pool = struct.pack('<6I', 1, 2, 9, 24, 5, 27) + first + second
        head = struct.pack('<4sI8x', b'BAR\x01', 2) + struct.pack('<HH4sII', 2, 0, b'sys\0', 48, len(pool))
        archive = head + struct.pack('<HH4sII', 24, 0, b'md_m', 96, 0) + pool
        pin = hashlib.sha256(second).hexdigest()
        result = reword(archive, [[5, pin, 5, 1, '9c9d']])
        self.assertEqual(result[:24], archive[:24])
        self.assertEqual(struct.unpack_from('<II', result, 24), (48, len(pool) + 1))
        self.assertEqual(struct.unpack_from('<6I', result, 48), (1, 2, 5, 24, 9, 32))
        self.assertEqual(result[72:], second[:5] + bytes([0x9c, 0x9d, 0]) + first)
        with self.assertRaises(ValueError):
            reword(archive, [[9, pin, 0, 0, '']])


if __name__ == '__main__':
    unittest.main()
