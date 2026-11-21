"""Convert an owned KH3 Xion export to the KH2 field and cutscene look packs."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from assets import Game, digest

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS / 'xion'))


def build(args, stage):
    from looks import Archive, parts, serialize, sheets, textures_native
    from hood import hooded, header

    recipe = json.loads((TOOLS.parent/'patches/xion.json').read_text())
    game = Game(args.game)
    inputs = stage/'inputs'
    for name, (kind, path, pin) in recipe['inputs'].items():
        blob = game.read(path, pin) if kind == 'game' else (args.nmc/path).read_bytes()
        if digest(blob) != pin:
            raise ValueError(f'unsupported donor: {path}')
        dest = inputs/name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(blob)
    for name, pin in {
        'H_EX500':'aa863da7839eaea61fd4dbd84e62e45d6eac3276c8359ea41dc6c4a53720615d',
        'H_EX510':'700fbdca1ee1ca340884c26e707496935b1714802d828ae5c604a803eda679d3',
        'H_ZZ010':'18275859018637853cbacc497dcf1816a0f7f267ec11fd506b2b572d91d2ee16',
    }.items():
        name = f'obj/{name}.mdlx'
        (inputs/name).write_bytes(game.read(name,pin))
    for script, target in [('to_glb.py','skin.glb'),('to_static_glb.py','static.glb')]:
        subprocess.run([args.blender,'-b','-noaudio','-t','2','--python-exit-code','1','-P',
                        TOOLS/'xion'/script,'--',(args.gltf.resolve() if target=='skin.glb' else stage/'skin.glb'),stage/target],check=True)
    groups = parts(stage)
    texture = textures_native(inputs,args.textures)
    hd = stage/'sheets'
    hd.mkdir()
    for i, sheet in enumerate(sheets(args.textures)):
        sheet.save(hd/f'-{i}.dds',format='DDS',pixel_format='DXT5')

    cache = {}
    for name in recipe['inputs']:
        for look in ('xion','xion-hooded'):
            dest = stage/'looks'/look/name
            dest.parent.mkdir(parents=True,exist_ok=True)
            if not name.endswith('.mdlx'):
                shutil.copyfile(inputs/name,dest)
                continue
            a = Archive((inputs/name).read_bytes())
            old,_ = header(a.members[4])
            stem = dest.stem
            if stem == 'H_EX500':
                field,_ = header(Archive((inputs/'obj/P_EX100.mdlx').read_bytes()).members[4])
                template = bytearray(old.template)
                template[176+old.aux:144+old.bo] = field.template[176+field.aux:144+field.bo]
                old.template = bytes(template)
            key = (stem if stem.startswith(('H_','P_EX110')) else 'field',
                   old.template[:176],old.template[144+old.aux:144+old.bo],old.bones)
            if key not in cache:
                body,_ = serialize(inputs,old,stem,groups)
                cache[key] = (body,hooded(inputs,stem,body))
            a.members[4] = cache[key][look=='xion-hooded']
            a.members[7] = texture
            if stem in recipe['pad16_before_type23']:
                a.rows = [(kind,link,tag,pad+bytes(16) if kind==23 else pad)
                          for kind,link,tag,pad in a.rows]
            dest.write_bytes(a.write())
            folder = stage/'looks'/look/'remastered'/name
            folder.mkdir(parents=True,exist_ok=True)
            for sheet in hd.iterdir():
                try:
                    os.link(sheet,folder/sheet.name)
                except OSError:
                    shutil.copyfile(sheet,folder/sheet.name)
    for name, pin in recipe['models'].items():
        if digest((stage/'looks'/name).read_bytes()) != pin:
            raise ValueError(f'converted asset differs from release: {name}')
    (stage/'looks').rename(args.out)
    print(f'{args.out}: {len(recipe["models"])} model and texture fingerprints verified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('gltf',type=Path)
    for name in ('game','nmc','textures','out'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--blender',default='blender')
    args = parser.parse_args()
    if args.out.exists():
        parser.error('choose a fresh output directory')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(dir=args.out.parent) as work:
            build(args,Path(work).resolve())
    except (ValueError,OSError,ImportError,subprocess.CalledProcessError) as error:
        parser.exit(1,f'Xion build failed: {error}\n')
