"""Build the playable stage-2 OpenKH overlay from the player's KH collection."""
import argparse
from collections import Counter
import csv
from functools import lru_cache
import hashlib
import io
import json
from pathlib import Path
import runpy
import subprocess
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parent
S2=ROOT/'stage2'
sys.path[:0]=[str(S2),str(ROOT/'tools')]
from PIL import Image
from assets import Game, apply_delta
from magick import bc3
from materials import materials, render
from paint import build as paint
from slots import encode
from standalone_sources import verify

def rows(name):
    with (S2/name).open(newline='') as f:
        yield from csv.DictReader(f,delimiter='\t')

def build(args,work):
    clean=args.extracts or work/'clean'
    mod=work/'overlay'
    stage1=runpy.run_path(str(ROOT/'tools/rmc-standalone.py'))['build']
    stage1(SimpleNamespace(extracts=clean,packages=args.packages,loose=args.loose,out=mod),work)
    installed=mod/'installed'
    pins=json.loads((mod/'provenance.json').read_text())
    game=Game(args.packages or args.loose or clean)
    game.loose.extend(Game(clean).loose)
    def emit(path,data,pin):
        verify(data,pin,path)
        target=installed/path
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(data)
        pins[path]=pin
    if args.records:
        for r in rows('collection.tsv'):
            emit(r['path'],(args.records/r['pin']).read_bytes(),r['pin'])
    @lru_cache(maxsize=3)
    def stock(source,pin):
        file=clean/source
        if file.is_file():
            return verify(file.read_bytes(),pin,source)
        rel=source.split('/',1)[1].removeprefix('original/')
        return game.read(rel,pin)
    @lru_cache(maxsize=2)
    def decoded(source,pin):
        return {(str(s),str(i)):im for s,i,im in materials(stock(source,pin),Counter())}
    for r in rows('images.tsv'):
        if r['path'] in pins: continue  # Prefer exact stage-1 results when available.
        if r['slot']:
            src=decoded(r['source'],r['pin'])[r['slot'],r['material']]
        else:
            with Image.open(io.BytesIO(stock(r['source'],r['pin']))) as img:
                src=img.convert('RGBA')
        im=render(src,(int(r['width']),int(r['height'])),r['method'])
        levels=int(r['mip_field'])
        payload=bc3(im,levels) if r['encoder']=='magick' else encode(im,r['format'],levels)
        emit(r['path'],payload,r['output'])
    if args.days_rom:
        import days
        for p,data,pin in days.build(args.days_rom):
            emit(p,data,pin)
    else:
        for p,_,_,_,pin in paint(installed):
            pins[p]=pin
    for script in ('build_rooms.py','build_mission.py'):
        subprocess.run([sys.executable,'-B',str(S2/script),'--clean',str(clean),'--out',str(installed)],check=True)
    for p,h in json.loads((S2/'room-pins.json').read_text()).items():
        verify((installed/p).read_bytes(),h,p)
        pins[p]=h
    for r in rows('opening.tsv'):
        emit(r['path'],game.read(r['donor'],r['pin']),r['pin'])
    import t100_rooms
    for p,data,pin in t100_rooms.build(game,work/'event-cache'):
        emit(p,data,pin)
    import wording
    word_pins=json.loads((S2/'wording-pins.json').read_text())
    for p,data in wording.build(clean):
        emit(p,data,word_pins[p])
    release=ROOT.parent if (ROOT.parent/'dll/rmc-native.dll').is_file() else ROOT/'runtime'
    for p,pin in json.loads((S2/'native.json').read_text()).items():
        data=verify((release/p).read_bytes(),pin,p)
        if p=='dll/rmc-native.dll':
            data=apply_delta(data,release/'patches/base-only.xz')
            pin='77ef7aa06de068b9e05c1aced4a7556e203244de42e86d191b93a441e7f0f1f0'
        emit(p,data,pin)
    manifest=json.loads((mod/'mod.yml').read_text())
    manifest.update(title='Roxas May Cry — standalone stage 2',assets=[
        dict(name=p,method='copy',source=[dict(name='installed/'+p)]) for p in sorted(pins)])
    (mod/'mod.yml').write_text(json.dumps(manifest,indent=2)+'\n')
    (mod/'provenance.json').write_text(json.dumps(pins,sort_keys=True,indent=2)+'\n')
    missing=mod/'MISSING.md'
    missing.write_text('\n'.join(line for line in missing.read_text().splitlines()
        if not line.startswith('| `') or line.split('`')[1] not in pins)+'\n')
    if {p.relative_to(installed).as_posix() for p in installed.rglob('*') if p.is_file()} != pins.keys():
        raise ValueError('copy map differs from installed tree')
    mod.rename(args.out)
    print(len(pins),'verified stage-2 destinations')

if __name__=='__main__':
    cli=argparse.ArgumentParser(description=__doc__)
    source=cli.add_mutually_exclusive_group(required=True)
    source.add_argument('--packages',type=Path)
    source.add_argument('--extracts',type=Path)
    cli.add_argument('--loose',type=Path)
    cli.add_argument('--records',type=Path,help='directory of 68 authenticated collection records from fetch_records.py')
    cli.add_argument('--days-rom',type=Path,help='optional European revision 0 Days cartridge dump')
    cli.add_argument('--out',type=Path,required=True)
    a=cli.parse_args()
    if a.loose and not a.extracts:cli.error('--loose requires --extracts')
    if a.out.exists() or any(a.out.resolve().is_relative_to(p.resolve()) for p in (a.packages,a.extracts,a.loose) if p):
        cli.error('choose a fresh output outside the input directories')
    a.out.parent.mkdir(parents=True,exist_ok=True)
    with TemporaryDirectory(dir=a.out.parent) as tmp:build(a,Path(tmp))
