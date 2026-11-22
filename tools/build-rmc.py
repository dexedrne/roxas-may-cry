"""Build an OpenKH mod ZIP locally from authenticated game and NMC donors."""
import argparse
from functools import lru_cache
import json
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
import zipfile

from assets import Game, apply_delta, digest
from title import compose, decode
import ps2
import text

ROOT = Path(__file__).resolve().parents[1]
TITLE = 'menu/us/title.2ld'
ATLAS = 'remastered/' + TITLE + '/-7.png'
WORD = 'remastered/menu/fm/title.2ld/-7.png'
DESCRIPTION = ("Version 1.2.4. Roxas's story played through to the end: the late game, The World That Never Was "
               "and the ending finished, PS2-version behaviours restored, four modes (Critical, Less Than Nothing, "
               "Xion Knows Best, Xion Always Cries), live Character switching and an in-game settings menu with cheats.")


def checked(path, pin):
    data = path.read_bytes()
    if digest(data) != pin:
        raise ValueError(f'unsupported input: {path}')
    return data


def reconstruct(args, temp):
    tools = args.decomposition.resolve()/'tools'
    def invoke(name,*params):
        subprocess.run([sys.executable,'-B',tools/name,*params],check=True)
    clean = args.clean
    if clean is None:
        clean = temp/'clean'
        indexes = json.loads((tools.parent/'config/package-index.json').read_text())
        for suffix in ('first','second','third','fourth','fifth','sixth'):
            stem = 'kh2_'+suffix
            pairs = sorted(args.game.rglob(stem+'.hed'))
            pairs = [p for p in pairs if hashlib.sha256(p.read_bytes()).hexdigest()==indexes[p.name]]
            if not pairs:
                raise ValueError(f'missing supported Steam package index: {stem}.hed')
            invoke('egs_extract.py','--hed',pairs[0],'--pkg',pairs[0].with_suffix('.pkg'),
                   '--names',tools.parent/'docs/needed-names.txt','--out',clean/stem)
    invoke('extract-from-your-NMC.py','--nmc',args.nmc,'--cache',temp/'donors')
    invoke('build-mod-from-vanilla.py','--vanilla',clean,'--assets',temp/'donors',
           '--preset','nmc','--out',temp/'nmc')
    return temp/'nmc/installed'


def build(args,temp):
    mod = args.rebuilt or reconstruct(args,temp)
    game = Game(args.game)
    recipes = json.loads((ROOT/'patches/release.json').read_text())
    retro = json.loads((ROOT/'patches/ps2.json').read_text()) if args.ps2 else {}
    if args.looks:
        recipes.update(json.loads((ROOT/'patches/optional.json').read_text()))
    # The upstream manifest spells its Windows destinations with backslashes.
    order = (ROOT/'patches/destinations.txt').read_text().splitlines()
    active = [n for n in order if n.replace('\\','/') in recipes]
    if len(active)+2 != len(recipes):
        raise ValueError('release layout and recipes disagree')
    description = DESCRIPTION if args.looks else (
        "Version 1.2.4. Roxas's story through the ending and postgame, with restored PS2 "
        "battle rules, Less Than Nothing difficulty and an in-game settings menu.")
    header = f'title: Roxas May Cry\ngame: kh2\ndescription: "{description}"\nassets:\n'
    manifest = header+''.join(f'- name: {n}\n  method: copy\n  source:\n  - name: {n}\n' for n in active)

    if args.looks:
        # The original ZIP has one set of HD body sheets. Both rigs use those sheets.
        models = json.loads((ROOT/'patches/xion.json').read_text())['models']
        for name, pin in sorted(models.items()):
            if not (name.startswith('xion-hooded/') and name.endswith('.dds')):
                continue
            source = 'dll/rmc/looks/' + name.replace('xion-hooded/', 'xion/', 1)
            if recipes[source][2] != pin:
                raise ValueError(f'hooded sheet has no matching ZIP source: {name}')
            look, relative = name.split('/remastered/', 1)
            for dest in ('dll/rmc/looks/'+name, 'remastered/dll/rmc/looks/'+look+'/'+relative):
                manifest += f'- name: {dest}\n  method: copy\n  source:\n  - name: {source}\n'

    @lru_cache(maxsize=3)
    def logos(name):
        word = decode(game.read(WORD,'71b91df51e0b6e64bbe196797cb50e5e69e63354d9ea9e59d9f253bb95090a82'))
        atlas = decode(checked(mod/ATLAS,'e7aba349b65d20de4adf16725d5d50da373bbd1edc592daea42a2465c65f0379'))
        return compose(name,word,atlas,checked(mod/TITLE,'45eeab550359e1f38dd42bb1702c06ac373b69121f0521e8bb00804852fb5ca1'))

    def asset(name):
        kind,path,pin,*delta = recipes[name]
        if kind=='game':
            data = game.read(path,pin)
        elif kind=='game-pair':
            data = b''.join(game.read(member, expected) for member, expected in path)
            if digest(data)!=pin:
                raise ValueError(f'combined donors differ: {name}')
        elif kind=='logo':
            stem,ext = path.split('.')
            data = logos(stem)[ext=='2ld']
        else:
            root = {'mod':mod,'repo':ROOT,'look':args.looks}[kind]
            data = checked(root/path,pin)
        if name in retro:
            source, ps2_pin, pin = retro[name]
            donor = ps2.read(args.ps2,source,ps2_pin)
            data = ps2.costume(data,donor) if name.endswith('.mdlx') else donor
        if delta:
            pin,patch = delta
            if patch=='text.json':
                data = text.reword(data,json.loads((ROOT/'patches'/patch).read_text())[name])
            else:
                data = apply_delta(data,ROOT/'patches'/patch)
        if digest(data)!=pin:
            raise ValueError(f'output fingerprint mismatch: {name}')
        if name=='dll/rmc-native.dll' and not args.looks:
            data = apply_delta(data,ROOT/'patches/base-only.xz')
            if digest(data)!='77ef7aa06de068b9e05c1aced4a7556e203244de42e86d191b93a441e7f0f1f0':
                raise ValueError('base-only DLL fingerprint mismatch')
        return data

    target = temp/'roxasmaycry.zip'
    with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        z.writestr('mod.yml',manifest.encode())
        for name in ['icon.png','preview.png',*(n.replace('\\','/') for n in active)]:
            z.writestr(name,asset(name))
    target.replace(args.output)
    print(f'{args.output}: {len(recipes)+1} entries')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game',type=Path,help='Steam collection directory or clean KH2 extraction')
    source=parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--decomposition',type=Path,help='local nmc-decomposed clone')
    source.add_argument('--rebuilt',type=Path,help='installed/ from a completed decomposition build')
    parser.add_argument('--nmc',type=Path,help='original NMC donor directory')
    parser.add_argument('--clean',type=Path,help='use an existing six-package decomposition extraction')
    parser.add_argument('--ps2',type=Path,help='owned PS2 NMC disc image or extracted files for original costume and icon')
    parser.add_argument('--looks',type=Path,help='output directory of build-xion.py')
    parser.add_argument('--output',type=Path,default=Path('roxasmaycry.zip'))
    args=parser.parse_args()
    if args.decomposition and not args.nmc:
        parser.error('--decomposition requires --nmc with the original donor files')
    if args.output.exists():
        parser.error('choose a fresh output ZIP')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(dir=args.output.parent) as work:
            build(args,Path(work).resolve())
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        parser.exit(1,f'Build failed: {error}\n')
