"""Alternate BC3 endpoint fit for high-agreement SD material."""
import subprocess

from PIL import Image

from slots import encode


def bc3(image, mips):
    # Use the same validated DDS header and premultiplied box mip policy as the primary encoder.
    base = encode(image, "DXT5", mips)
    payload = bytearray(base[:128])
    for level in range(max(1,mips)):
        if level:
            image = image.resize(tuple(max(1,n//2) for n in image.size), Image.Resampling.BOX)
        w,h = image.size
        data = subprocess.check_output(["magick","-limit","thread","1","-size",f"{w}x{h}","-depth","8","rgba:-",
                                        "-define","dds:compression=dxt5","-define","dds:mipmaps=0","dds:-"],
                                       input=image.tobytes())
        if data[84:88] != b"DXT5" or len(data) != 128+((w+3)//4)*((h+3)//4)*16:
            raise ValueError("unexpected alternate encoder output")
        payload.extend(data[128:])
    return bytes(payload)
