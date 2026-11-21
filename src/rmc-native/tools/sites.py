"""Build and audit Steam-specific .text signatures using the owner's PE as input."""
import argparse
import bisect
import hashlib
import re
import struct
import subprocess
from pathlib import Path

DIGEST = '9002b2de6a1f91a790bd0673de125d1cf833f7942bfec827cdcf6ba64d5849ed'
BASE = 0x140000000
parser = argparse.ArgumentParser()
parser.add_argument('exe')
parser.add_argument('output', type=Path)
parser.add_argument('sites', nargs='+', type=Path)
args = parser.parse_args()
pe = Path(args.exe).read_bytes()
if hashlib.sha256(pe).hexdigest() != DIGEST:
    raise SystemExit('Unsupported Steam executable digest')
u16 = lambda p: struct.unpack_from('<H', pe, p)[0]
u32 = lambda p: struct.unpack_from('<I', pe, p)[0]
nt = u32(60)
sections = nt + 24 + u16(nt + 20)
size, start, rawsize, raw = struct.unpack_from('<IIII', pe, sections+8)
assert pe[sections:sections+5] == b'.text'
code = pe[raw:raw+size]
# Loader fixups can rewrite absolute addresses before Panacea loads the plugin.
# Choose signatures outside such bytes so the same match survives ASLR.
relocations = bytearray(size)
for i in range(u16(nt+6)):
    sh = sections+40*i
    if pe[sh:sh+6] != b'.reloc': continue
    end = u32(sh+20)+u32(sh+8)
    q = u32(sh+20)
    while q+8 <= end:
        page, length = struct.unpack_from('<II',pe,q)
        if length < 8: break
        for entry, in struct.iter_unpack('<H',pe[q+8:q+length]):
            if not entry>>12: continue
            pos = page+(entry&4095)-start
            if 0 <= pos < size:
                if entry>>12 != 10: raise ValueError('unexpected .text relocation type')
                relocations[pos:pos+8] = bytes([1])*8
        q += length
image = bytearray(u32(nt+24+56))
image[:u32(nt+24+60)] = pe[:u32(nt+24+60)]
for i in range(u16(nt+6)):
    virtual,rva,n,offset=struct.unpack_from('<IIII',pe,sections+40*i+8)
    length=min(virtual,n)
    image[rva:rva+length]=pe[offset:offset+length]
ids = {}
for path in args.sites:
    for line in path.read_text().splitlines():
        fields = line.split('#')[0].split()
        if fields:
            key = int(fields[0],16)
            ids[key] = ids.get(key,False) or 'hook' in fields[1:]
def disasm(*options):
    return ['objdump','-dw','-Mintel','-j','.text',*options,args.exe]
# Non-code targets use the nearest RIP-relative operand as their anchor.
refs = {}
if any(key >= start+size for key in ids):
    with subprocess.Popen(disasm(), stdout=subprocess.PIPE, text=True) as p:
        for line in p.stdout:
            if '[rip' not in line or '# 0x' not in line: continue
            addr, encoding, asm = line.strip().split('\t',2)
            target = int(asm.split('# 0x')[1].split()[0],16)-BASE
            pos = int(addr[:-1],16)-BASE
            op = bytes.fromhex(encoding)
            disp = struct.pack('<i',target-pos-len(op))
            off = op.find(disp)
            if off>0 and op.count(disp)==1:
                refs.setdefault(target,(pos,off,len(op)))
        if p.wait(): raise SystemExit('objdump failed')
targets = sorted(refs)
def nearest(key):
    i = bisect.bisect_left(targets,key)
    return min(targets[max(0,i-1):i+1],key=lambda x:abs(key-x))
def prologue(key):
    end=key
    fix=0
    for line in subprocess.check_output(disasm('--start-address='+hex(BASE+key),'--stop-address='+hex(BASE+key+20)),text=True).splitlines():
        if '\t' not in line: continue
        addr, encoding, asm = line.strip().split('\t',2)
        op=bytes.fromhex(encoding)
        assert int(addr[:-1],16)==BASE+end
        if '[rip' in asm or re.match(r'(?:j\w+|call)\s+0x',asm):
            target=int(asm.split('0x')[-1].split()[0],16)
            disp=target-(BASE+end+len(op))
            if not -(1<<31)<=disp<1<<31 or struct.pack('<i',disp) not in op:
                raise ValueError(f'unsupported short branch in hook {key:x}')
            if fix: raise ValueError('multiple relative operands')
            fix=end-key+op.index(struct.pack('<i',disp))+1
        end+=len(op)
        if end-key>=5: return end-key,fix
    raise ValueError('truncated hook')
directory, directory_size = struct.unpack_from('<II', pe, nt+24+112+3*8)
unwind_rvas = {begin: unwind for begin, _, unwind in
               struct.iter_unpack('<III', image[directory:directory+directory_size])}

def unwind_prefix(key, span):
    """x64 UNWIND_INFO for the part that actually runs in the trampoline."""
    rva = unwind_rvas.get(key)
    if not rva: return b''
    version_flags, prolog, slots, frame = image[rva:rva+4]
    if version_flags & 7 != 1 or version_flags & 32 or frame:
        raise ValueError(f'unsupported unwind record at {key:x}')
    ops = bytearray()
    i = 0
    while i < slots:
        at = rva+4+2*i
        end_offset, encoded = image[at:at+2]
        op, info = encoded & 15, encoded >> 4
        width = {0:1, 1:2 if info==0 else 3, 2:1, 4:2, 5:3, 8:2, 9:3}[op]
        if end_offset <= span:
            ops.extend(image[at:at+2*width])
        i += width
    if not ops: return b''
    return bytes([1,min(prolog,span),len(ops)//2,0])+ops

def lit(b):
    return '"'+''.join('\\x%02x'%c for c in b)+'"'
rows=[]
for key,hook in sorted(ids.items()):
    pos,off,end,delta=key,0,0,0
    if not start<=key<start+size:
        target=nearest(key)
        if abs(key-target)>0x2000: raise ValueError(f'no close data reference for {key:x}')
        pos,off,end=refs[target]
        delta=key-target
    # Extend in both directions through common prologues and padding.
    for shift in (0,-32,32,-128,128,-512,512):
        for width in (48,64,96,160,256,512):
            anchor=max(start,min(pos+shift-min(8,width//2),start+size-width))
            at=anchor-start
            window=code[at:at+width]
            if any(relocations[at:at+width]): continue
            if image.find(window)==anchor and image.find(window,anchor+1)<0: break
        else: continue
        break
    else: raise ValueError(f'ambiguous or relocation-sensitive signature {key:x}')
    resolved=anchor+(pos-anchor)
    if end: resolved+=end+struct.unpack_from('<i',code,pos-start+off)[0]+delta
    if resolved!=key: raise ValueError(f'incorrect site {key:x} -> {resolved:x}')
    span,fix=prologue(key) if hook else (0,0)
    unwind=unwind_prefix(key,span) if hook else b''
    rows.append(f'{{0x{key:x},{lit(window)},{width},{pos-anchor},{off},{end},{delta},{span},{fix},{lit(unwind)},{len(unwind)}}},')
header=f'#define STEAM_SIZE 0x{u32(nt+24+56):x}u\n#define STEAM_STAMP 0x{u32(nt+8):x}u\n#define TEXT_SIZE {size}u\n#define TEXT_START {start}u\n'
temp = args.output.with_suffix(".pending")
temp.write_text(header+'\n'.join(rows)+'\n')
temp.replace(args.output)
print(f'{len(rows)} unique Steam .text signatures, {sum(ids.values())} relocated prologues')
