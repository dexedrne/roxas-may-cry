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


if __name__ == '__main__':
    unittest.main()
