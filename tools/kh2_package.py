"""Streaming Python port of OpenKh.Egs (Hed, EgsEncryption, EgsHdAsset, EgsTools).

OpenKH - Open Kingdom Hearts Research and Development
Copyright 2019-2022 OpenKH Team and Contributors
Adapted to read individual original KH2 assets.
This product includes software developed at
The OpenKH GitHub Repository (https://git.openkh.dev/).
Licensed under the Apache License, Version 2.0 (see LICENSE-OpenKH).
"""
import struct
import zlib

# OpenKh.Egs/EgsEncryption.cs, MasterKey and GenerateKey.
MASTER_KEY = bytes.fromhex(
    "7e8897550b06f108ebbb141cd87aec41"
    "34b2a346ef6bfee1cf53a50512d28e52"
    "4a80e981b0f0b49cff0f1513da734e77"
    "bed730e5f65a113767bc836f2776d0cd"
    "690d2e514290b8b64cadce5b1a1ff5af"
    "01f85e3a6e688be89fc9d9269229c833"
    "983254d4442566ac5f9921e48f1dc2d5"
    "a462f90261de59e7079afa2f953f86d3"
    "78a775edd62d6487bdc7c1aaf28c17cb"
    "318ac3cc04ee6aab5c2270ca9e716d85"
    "455db9a9a61047fb827d847bc6e238fc"
    "2b0e209dc5f339a8a06558437ce33618"
    "724979aed17440c4914f2463bfba2396"
    "50b357df1e03487f354d3ee6a1dd093c"
    "3d3b568d932a9b4b0c28b1e0608919db"
    "2cf76cb51b94c0dceab70af416fda200"
)


def egs_mask(seed):
    """Fold the eleven XOR round keys into one repeated 16-byte mask."""
    if len(seed) != 16:
        raise ValueError("invalid EGS seed length")
    key = bytearray(value or i for i, value in enumerate(seed))
    mask = key.copy()
    for constant in (1, 2, 4, 8, 16, 32, 64, 128, 0x1B, 0x36):
        word = bytearray(MASTER_KEY[key[i]] for i in (13, 14, 15, 12))
        word[0] ^= constant
        for i in range(16):
            key[i] ^= word[i % 4]
            word[i % 4] = key[i]
            mask[i] ^= key[i]
    return bytes(mask)


def decrypt_prefix(data, mask):
    """Decrypt the first chunk of an original or HD payload."""
    prefix = bytes(value ^ mask[i % 16] for i, value in enumerate(data[:256]))
    return prefix + data[256:]


HED = struct.Struct("<16sqii")
HD = struct.Struct("<32s4i")


def read_exact(stream, n):
    data = stream.read(n)
    if len(data) != n:
        raise ValueError(f"short read at {stream.tell() - len(data):#x}, expected {n} bytes")
    return data


def read_index(stream):
    entries = {}
    for row in iter(lambda: stream.read(HED.size), b""):
        if len(row) != HED.size:
            raise ValueError(f"truncated HED entry at {stream.tell() - len(row):#x}")
        md5, offset, stored, actual = HED.unpack(row)
        if md5 in entries:
            raise ValueError(f"duplicate HED digest {md5.hex()}")
        entries[md5] = offset, stored, actual
    return entries


def read_original(hed, entry):
    """Read the original member of one KH2 HED/PKG record."""
    offset, stored, expected = entry
    pkg = hed.with_suffix('.pkg')
    if offset < 0 or stored < 16 or offset + stored > pkg.stat().st_size:
        raise ValueError(f'invalid record bounds in {pkg}')
    with pkg.open('rb') as stream:
        stream.seek(offset)
        seed = read_exact(stream, 16)
        size, count, mode, _ = struct.unpack('<4i', seed)
        length = size if mode < 0 else mode
        header = 16 + count * HD.size
        if size != expected or size < 0 or count < 0 or mode < -2 or header + length > stored:
            raise ValueError(f'invalid original member in {pkg}')
        stream.seek(offset + header)
        data = read_exact(stream, length)
    if mode > -2:
        data = decrypt_prefix(data, egs_mask(seed))
    if mode >= 0:
        decoder = zlib.decompressobj(-15)
        data = decoder.decompress(data[2:], size + 1)
        if not decoder.eof or len(data) != size:
            raise ValueError(f'incomplete or oversized original member in {pkg}')
    return data


def read_hd(hed, entry, member):
    """Read a named remastered companion, respecting 16-byte package alignment."""
    offset, stored, expected = entry
    with hed.with_suffix('.pkg').open('rb') as stream:
        stream.seek(0, 2)
        if offset < 0 or stored < 16 or offset + stored > stream.tell():
            raise ValueError('invalid package record extent')
        stream.seek(offset)
        seed = read_exact(stream, 16)
        size, count, mode, _ = struct.unpack('<4i', seed)
        if size != expected or mode < -2 or size < 0 or not 0 <= count <= (stored-16)//HD.size:
            raise ValueError('invalid package record header')
        headers = [HD.unpack(read_exact(stream, HD.size)) for _ in range(count)]
        stream.seek(size if mode < 0 else mode, 1)
        for raw, _, _, size, mode in headers:
            if size < 0 or mode < -2:
                raise ValueError('invalid HD member header')
            stream.seek((-stream.tell()) % 16, 1)
            length = ((size if mode < 0 else mode) + 15) & ~15
            if stream.tell() + length > offset + stored:
                raise ValueError('HD member outside record')
            if raw.split(b'\0', 1)[0].decode().replace('\\', '/') != member:
                stream.seek(length, 1)
                continue
            data = read_exact(stream, length)
            if mode > -2:
                data = decrypt_prefix(data, egs_mask(seed))
            if mode >= 0:
                return zlib.decompress(data[2:], -15)
            return data
    return None
