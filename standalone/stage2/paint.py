"""Original Neverland material alternatives. The coordinates are in a repeating 128 x 64 tile."""
import argparse
import csv
import hashlib
import math
import random
import sys
from pathlib import Path
from PIL import Image, ImageDraw
HERE = Path(__file__).resolve().parent

from slots import encode, slot

def paint(name):
    rng = random.Random(name)
    im = Image.new('RGBA',(128,64))
    d = ImageDraw.Draw(im)
    wave = math.sin
    if name in ('cloud','cloud01','sky00'):
        for y in range(64):
            for x in range(128):
                u,v = x*math.tau/128,y*math.tau/64
                cloud = (wave(u+2*wave(v))+0.6*wave(3*u-v)+0.4*wave(5*v+u)+0.22*wave(7*u+4*v))/2.22
                if name == 'sky00':
                    shade = int(max(0,cloud)*10+18*y/64)
                    color = (8+shade,13+shade,32+shade,255)
                else:
                    light = int(42*cloud)
                    alpha = min(255,max(0,int(320*(cloud + (0.3 if name=='cloud01' else 0.05)))))
                    color = (163+light,164+light,192+light,alpha)
                im.putpixel((x,y),color)
        if name == 'sky00':
            for _ in range(95):
                x,y = rng.randrange(128),rng.randrange(54)
                lum = rng.randrange(65,205)
                d.point((x,y),fill=(lum,lum, min(255,lum+20)))
        return im
    if name == 'flag':
        d.rectangle((0,0,127,63),fill='#171721')
        for x,y in ((44,41),(83,41)):
            d.line((64,26,x,y), fill='#d6c7a4',width=3)
            d.ellipse((x-2,y-2,x+2,y+2), fill='#d6c7a4')
        d.ellipse((51,11,77,30),fill='#e6d7b7')
        d.rectangle((57,25,71,32),fill='#e6d7b7')
        for x in (55,66): d.ellipse((x,19,x+6,23),fill='#262533')
        return im
    if name == 'lamp00':
        for x in range(128):
            light = 0.5+0.5*wave(math.tau*x/64)
            d.line((x,0,x,63),fill=(int(130+95*light),int(82+66*light),29))
        for x in (9,72):
            d.polygon([(x,54),(x+7,17),(x+14,12),(x+20,22),(x+27,54)],outline='#805722',width=2)
        return im
    brass = name == 'wall06'
    wooden = name in ('wall00','bed02','box00')
    base = (97,72,30) if brass else (125,68,36) if wooden else (104,39,48)
    for y in range(64):
        for x in range(128):
            seam = 12 if (x%16 == 0 if name != 'bed02' else y%16==0) else 0
            n = rng.randrange(-3,4)+int(4*wave(x*0.3+wave(y*0.2)))-seam
            im.putpixel((x,y),tuple(c+n for c in base)+(255,))
    if name == 'box00':
        for x in range(0,128,40):
            d.rectangle((x+1,2,x+39,61),outline='#ba8252',width=3)
            d.line((x+4,58,x+35,5),fill='#bc8554',width=3)
    else:
        y = 28 if name=='wall08' else 8
        d.rectangle((0,0,128,y+2),fill='#835735' if wooden else '#766031')
        d.line((0,y+3,127,y+3),fill='#d2a563',width=2)
        for x in range(-16,128,16):
            d.arc((x,1,x+18,y+1),0,180,fill='#e0bd78',width=2)
        if name in ('ship03','ship04'):
            x = 85 if name=='ship03' else 32
            d.rectangle((x,0,x+8,63),fill='#603337',outline='#b8794e')
            for y in (6,58): d.ellipse((x+3,y,x+5,y+2),fill='#d8b97d')
    return im

def build(dest):
    results=[]
    for r in csv.DictReader((HERE/'materials.tsv').open(),delimiter='\t'):
        size = int(r['width']), int(r['height'])
        image = paint(r['material']).resize(size,Image.Resampling.BICUBIC)
        data = encode(image,'BGRA8',int(r['mip_field']))
        if slot(data) != ('BGRA8',int(r['mip_field'])): raise ValueError(r['path'])
        if hashlib.sha256(data).hexdigest()!=r['output_sha256']:
            raise ValueError('original material fingerprint differs: '+r['path'])
        target=dest/r['path']; target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        results.append((r['path'],r['material'],'own','original tile',hashlib.sha256(data).hexdigest()))
    return results

if __name__ == '__main__':
    cli=argparse.ArgumentParser(description=__doc__)
    cli.add_argument('out',type=Path)
    cli.add_argument('--ledger',type=Path)
    args=cli.parse_args()
    records=build(args.out)
    if args.ledger:
        with args.ledger.open('w') as f:
            w=csv.writer(f,delimiter='\t'); w.writerow(('path','material','result','method','sha256'));w.writerows(records)
    print(len(records),'original replacement slots')
