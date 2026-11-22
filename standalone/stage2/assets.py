"""Authenticated KH2 package and binary-delta inputs."""
from functools import lru_cache
import hashlib
import io
import lzma
from pathlib import Path

from kh2_package import read_exact, read_hd, read_index, read_original


def digest(data):
    return hashlib.sha256(data).hexdigest()


class Game:
    def __init__(self, root):
        if not root.is_dir():
            raise ValueError(f'game directory does not exist: {root}')
        self.loose = [root, root / 'original', root / 'kh2', root / 'kh2/original']
        self.loose.extend(sorted(root.glob('kh2_*/original')))
        self.loose.extend(sorted(root.glob('kh2_*')))
        self.root = root

    @lru_cache(maxsize=1)
    def packages(self):
        result = {}
        for hed in sorted(self.root.rglob('kh2_*.hed')):
            with hed.open('rb') as stream:
                for key, record in read_index(stream).items():
                    result.setdefault(key, []).append((hed, record))
        return result

    def read(self, name, pin):
        for directory in self.loose:
            path = directory / name
            if path.is_file():
                data = path.read_bytes()
                if digest(data) == pin:
                    return data
        if name.startswith('remastered/'):
            parts = name.split('/')[1:]
            for i in range(len(parts)-1, 0, -1):
                key = hashlib.md5('/'.join(parts[:i]).encode()).digest()
                for hed, record in self.packages().get(key, []):
                    data = read_hd(hed, record, '/'.join(parts[i:]))
                    if data is not None:
                        # Authenticate the payload length, including EGS alignment where required.
                        for trim in range(min(16,len(data)+1)):
                            candidate = data if trim == 0 else data[:-trim]
                            if digest(candidate) == pin:
                                return candidate
        key = hashlib.md5(name.encode()).digest()
        for hed, record in self.packages().get(key, []):
            data = read_original(hed, record)
            if digest(data) == pin:
                return data
        raise ValueError(f'missing or unsupported KH2 asset: {name}')


def apply_delta(base, patch):
    stream = io.BytesIO(lzma.decompress(patch.read_bytes()))
    if read_exact(stream, 4) != bytes.fromhex('72730236'):
        raise ValueError(f'invalid delta: {patch.name}')

    def integer(width):
        return int.from_bytes(read_exact(stream, width), 'big')

    output = bytearray()
    while True:
        op = integer(1)
        if op == 0:
            if stream.read(1):
                raise ValueError('trailing delta data')
            return bytes(output)
        if op <= 68:
            size = op if op <= 64 else integer(1 << (op - 65))
            output.extend(read_exact(stream, size))
        elif op <= 84:
            offset_width, size_width = divmod(op - 69, 4)
            offset = integer(1 << offset_width)
            size = integer(1 << size_width)
            if offset + size > len(base):
                raise ValueError('delta references missing source bytes')
            output.extend(base[offset:offset + size])
        else:
            raise ValueError(f'unknown delta instruction {op}')
