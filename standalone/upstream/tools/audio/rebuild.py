"""Import pinned REFERENCE audio donors or reconstruct the D1 audio writer's loose files."""
import argparse
import sys
import tempfile
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from formats import require
from project import catalog, check_manifest, checked, destination, inventory


def recipes():
    targets = inventory()
    owners = {n: r for n, r in catalog('docs/inventory/baseline.tsv').items()
              if r['writer'] == 'audio'}
    rows = catalog('patches/audio/files.tsv')
    require(rows.keys() == owners.keys(), 'recipe paths differ from D1 audio ownership')
    for name, row in rows.items():
        base, source, op = row['base'], row['source'], row['operation']
        owner = owners[name]
        require(row['sha256'] == targets[name]['sha256'], f'{name}: wrong target pin')
        if owner['decision'] == 'addition':
            require(not base and not row['base_sha256'] and op in ('add', 'transplant'),
                    f'{name}: invalid addition')
        else:
            rel = name if name.startswith('remastered/') else 'original/' + name
            require(base == f"kh2_{owner['package']}/{rel}" and len(row['base_sha256']) == 64
                    and op in ('replace', 'retain', 'transplant'), f'{name}: invalid base')
        if op in ('replace', 'add'):
            require(source == name, f'{name}: incorrect donor path')
        else:
            require(source.startswith('kh2_') and all(part not in ('', '.', '..')
                    for part in source.split('/')), f'{name}: invalid clean source')
            require((op == 'retain') == (base == source), f'{name}: invalid clean operation')
    return rows


def emit(args):
    rows = recipes()
    importing = args.command == 'import'
    output = destination(args.cache if importing else args.out)
    require(not output.exists(), f'destination exists: {output}')
    inputs = [args.reference] if importing else [args.clean, args.cache]
    for root in inputs:
        require(root.is_dir(), f'missing input directory: {root}')
        require(not output.is_relative_to(root.resolve()), f'destination overlaps input: {root}')
    if importing:
        check_manifest(args.reference)

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as temp:
        stage = Path(temp) / 'stage'
        stage.mkdir()
        for name, row in rows.items():
            sha = row['sha256']
            donor = row['operation'] in ('add', 'replace')
            if importing:
                if not donor:
                    continue
                path = stage / sha
                if path.exists():
                    continue
                data = checked(args.reference / row['source'], sha)
            else:
                base = None
                if row['base']:
                    base = checked(args.clean / row['base'], row['base_sha256'])
                if row['operation'] == 'retain':
                    require(row['base_sha256'] == sha, f'{name}: retained bytes differ from pin')
                    data = base
                elif donor:
                    data = checked(args.cache / sha, sha)
                else:
                    data = checked(args.clean / row['source'], sha)
                path = stage / name
                path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        stage.rename(output)
    counts = Counter(r['operation'] for r in rows.values())
    if importing:
        print(f'{len(list(output.iterdir()))} distinct donor assets imported into {output}')
    else:
        print(f'{len(rows)} audio destinations in {output} ({dict(counts)})')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    cmds = p.add_subparsers(dest='command', required=True)
    imp = cmds.add_parser('import', help='create a private cache from the pinned REFERENCE checkout')
    imp.add_argument('--reference', type=Path, required=True)
    build = cmds.add_parser('build', help='build into a new directory below private/')
    build.add_argument('--clean', type=Path, required=True)
    build.add_argument('--out', type=Path, required=True)
    for cmd in (imp, build):
        cmd.add_argument('--cache', type=Path, required=True)
    try:
        emit(p.parse_args())
    except (ValueError, OSError, KeyError) as e:
        p.exit(1, f'audio: {e}\n')
