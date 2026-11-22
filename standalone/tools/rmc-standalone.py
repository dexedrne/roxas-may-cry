"""Build the authenticated package-one OpenKH overlay (Python 3.11+)."""
import argparse
from collections import Counter, defaultdict
import json
import os
from pathlib import Path
import struct
import subprocess
from tempfile import TemporaryDirectory

from standalone_sources import RECIPES, UPSTREAM, Sources, Unavailable, extract_clean, rows, verify


def build(args, tmp):
    os.environ['RMC_WORK'] = str(tmp)
    from project import inventory
    from standalone_writers import build as write_archives
    clean = args.extracts or tmp / 'clean'
    if args.packages:
        extract_clean(args.packages, clean)
    source = Sources(tmp, args.packages, clean, args.loose)
    stage = tmp / 'mod'
    stage.mkdir()
    installed = stage / 'installed'
    scope = {r['path']: r for r in rows(RECIPES / 'scope.tsv')}
    pins = inventory()
    missing = {}
    expected = {}
    work = {p for p,r in scope.items() if r['class'] == 'A'}
    textures = {r["path"]: r for r in rows(RECIPES / "textures.tsv")}
    if Counter(r['class'] for r in scope.values()) != {'A':286, 'B':621, 'C':305, 'D':3649}:
        raise ValueError('standalone scope ledger incomplete')
    donors = defaultdict(list)
    for member in rows(UPSTREAM / 'patches/models-ui/placements.tsv'):
        if member['source'] == 'donor':
            donors[member['path']].append(member['sha256'])
    for p,r in scope.items():
        if r['class'] == 'A':
            continue
        reason = r['status']
        if r['class'] == 'B':
            pin = pins[p]['sha256']
            try:
                if reason.startswith('member assembly'):
                    for sha in donors[p]:
                        source.resolve(sha)
                    work.add(p)
                    continue
                if p in textures or pin in source.sources:
                    data = source.texture(textures[p]) if p in textures else source.resolve(pin)
                    dest = installed / p
                    dest.parent.mkdir(exist_ok=True, parents=True)
                    dest.write_bytes(data)
                    expected[p] = pin
                    continue
                if reason != 'days-dds':
                    raise ValueError(f'missing exact recipe for {p}')
                reason = 'Only a Days cartridge source is known'
            except Unavailable as exc:
                reason = f'retail payload absent from diagnostic extracts ({exc})'
        missing[p] = r['class'] + ' | ' + reason
    write_archives(tmp, stage, clean, work, source.resolve)
    expected.update((p, pins[p]['sha256']) for p in work)
    for r in rows(RECIPES / 'arenas.tsv'):
        name = r['path']
        loc = '/'.join((r['package'], r['record_md5'], r['member']))
        try:
            data = source.record(loc, r['sha256'], r['patch_source'])
        except Unavailable:
            missing[name] = 'arena | retail payload absent from diagnostic extracts'
            continue
        if name in expected or name in scope:
            raise ValueError(f'overlapping arena destination {name}')
        dest = installed / name
        dest.parent.mkdir(exist_ok=True, parents=True)
        dest.write_bytes(data)
        expected[name] = r['sha256']
    if len(expected) + len(missing) != len(scope) + 105 or expected.keys() & missing.keys():
        raise ValueError('standalone destination accounting mismatch')
    actual = {p.relative_to(installed).as_posix() for p in installed.rglob('*') if p.is_file()}
    if actual != expected.keys() or len({p.casefold() for p in actual}) != len(actual):
        raise ValueError('invalid OpenKH destination set')
    for p, sha in expected.items():
        verify((installed / p).read_bytes(), sha, p)
    assets = [dict(name=p, method='copy', source=[dict(name='installed/'+p)]) for p in sorted(actual)]
    manifest = dict(title='Roxas May Cry — standalone stage 1', game='kh2', assets=assets)
    (stage/'mod.yml').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    (stage/'provenance.json').write_text(json.dumps(expected, sort_keys=True, indent=2)+'\n')
    (stage/'sources.json').write_text(json.dumps(source.used, sort_keys=True, indent=2)+'\n')
    (stage/'MISSING.md').write_text('# Not in this build yet\n\n'
        'OpenKH leaves these destinations to the base game. Mod-only paths stay absent.\n\n'
        '| Destination | Class | Reason |\n|---|---|---|\n' +
        ''.join(f'| `{p}` | {reason} |\n' for p,reason in sorted(missing.items())), encoding='utf-8')
    stage.rename(args.out)
    print(f'{len(actual)} verified copy destinations. {len(missing)} not in this build yet (MISSING.md).')


if __name__ == '__main__':
    cli = argparse.ArgumentParser(description=__doc__)
    mode = cli.add_mutually_exclusive_group(required=True)
    mode.add_argument('--packages', type=Path, help='collection directory containing kh2_first.hed')
    mode.add_argument('--extracts', type=Path, help='diagnostic subset from six authenticated KH2 extracts')
    cli.add_argument('--loose', type=Path, help='additional retail files for --extracts')
    cli.add_argument('--out', type=Path, required=True)
    args = cli.parse_args()
    if args.loose and not args.extracts:
        cli.error('--loose requires --extracts')
    args.out = args.out.resolve()
    if args.out.exists() or any(args.out.is_relative_to(p.resolve()) for p in
                                (args.extracts, args.packages, args.loose) if p):
        cli.error('choose a fresh destination outside the input directories')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    try:
        with TemporaryDirectory(dir=args.out.parent) as temp:
            build(args, Path(temp))
    except (OSError, ValueError, KeyError, struct.error, subprocess.CalledProcessError) as exc:
        cli.exit(1, f'build failed: {exc}\n')
