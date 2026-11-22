"""Produce a fresh DDS mip chain or RGBA PNG from a clean render."""
import io
import struct

from PIL import Image


def slot(data):
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        if data[24:26] != b"\x08\x06":
            raise ValueError("unsupported PNG channels")
        return "PNG", 1
    if data[:4] != b"DDS ":
        raise ValueError("unsupported texture container")
    pf = struct.unpack_from("<8I", data, 76)
    if pf == (32, 4, int.from_bytes(b"DXT5", "little"), 0, 0, 0, 0, 0):
        fmt = "DXT5"
    elif pf == (32, 65, 0, 32, 0xff0000, 0xff00, 0xff, 0xff000000):
        fmt = "BGRA8"
    else:
        raise ValueError(f"unsupported DDS format {pf}")
    return fmt, struct.unpack_from("<I", data, 28)[0]


def encode(image, fmt, mips):
    buf = io.BytesIO()
    if fmt == "PNG":
        image.save(buf, format="PNG")
        return buf.getvalue()
    chunks = []
    for level in range(max(1, mips)):
        if level:
            image = image.resize(tuple(max(1, n // 2) for n in image.size), Image.Resampling.BOX)
        buf.seek(0)
        buf.truncate()
        image.save(buf, format="DDS", **({"pixel_format": "DXT5"} if fmt == "DXT5" else {}))
        data = buf.getvalue()
        length = ((image.width+3)//4)*((image.height+3)//4)*16 if fmt == "DXT5" else image.width*image.height*4
        if len(data) != 128 + length:
            raise ValueError("DDS level length mismatch")
        if not level:
            header = bytearray(data[:128])
            if fmt == "DXT5":
                struct.pack_into("<I", header, 20, length)
                struct.pack_into("<I", header, 88, 0)
            struct.pack_into("<I", header, 28, mips)
            if mips:
                flags = struct.unpack_from("<I", header, 8)[0]
                struct.pack_into("<I", header, 8, flags | 0x20000)
            if mips > 1:
                struct.pack_into("<I", header, 108, 0x401008)
        chunks.append(data[128:])
    return bytes(header) + b"".join(chunks)
