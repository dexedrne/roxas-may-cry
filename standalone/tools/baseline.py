"""Authenticate the PC/US extracts and reconstruct each game data file."""
import csv
import os
from pathlib import Path
import subprocess
import sys

UPSTREAM = Path(__file__).resolve().parents[1] / 'upstream'
sys.path.insert(0, str(UPSTREAM / 'tools'))
from formats import require
from project import catalog, checked, inventory, package_manifest


def source_assets(clean, work, extracted=None):
    sources = {}
    with (UPSTREAM / 'patches/models-ui/packages.tsv').open() as stream:
        for pkg in csv.DictReader(stream, delimiter='\t'):
            root = clean / pkg['package']
            rows, digest = package_manifest(root / 'manifest.tsv')
            require(digest == pkg['sha256'], f'unsupported PC/US extract: {root}')
            for row in rows:
                sources[row['sha256']] = root / row['path']
    cache = work / 'assets'
    missing = []
    with (UPSTREAM / 'asset-inputs.tsv').open() as stream:
        for row in csv.DictReader(stream, delimiter='\t'):
            digest = row['sha256']
            source = sources.get(digest)
            if source is None and extracted is not None:
                candidate = extracted / row['path']
                if candidate.is_file():
                    source = candidate
            if source is None:
                missing.append(row['path'])
                continue
            data = checked(source, digest)
            target = cache / row['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    require(not missing, f'{len(missing)} asset inputs have no matching whole file in the six clean extracts. '
            f'Game extraction or conversion recipes are required. First missing input: {missing[0] if missing else ""}')
    return cache


def reconstruct(clean, cache, work):
    env = {**os.environ, 'RMC_WORK': str(work)}
    def run(script, *args):
        subprocess.run([sys.executable, '-B', UPSTREAM / 'tools' / script, *args], check=True, env=env)

    scripts = work / 'scripts'
    run('ai/members.py', 'build', '--clean', clean, '--cache', cache/'ai', '--output', scripts)
    owners = catalog('docs/inventory/baseline.tsv')
    pins = inventory()
    require(owners.keys() == pins.keys(), 'baseline inventory mismatch')
    base = work / 'baseline'
    for area, script in [('ai','ai/common.py'), ('tables','tables/build.py'), ('rooms','rooms/rooms.py'),
                         ('missions-messages','missions-messages/area.py'), ('models-ui','models-ui/area.py'),
                         ('audio','audio/rebuild.py')]:
        dest = work / 'writers' / area
        args = ['--clean', clean]
        if area not in ('ai','tables'):
            args = ['build', *args, '--cache', cache / area]
        if area == 'models-ui':
            args += ['--ai', scripts, '--output', dest]
        else:
            args += ['--out', dest]
        run(script, *args)
        names = {p.relative_to(dest).as_posix() for p in dest.rglob('*') if p.is_file()}
        require(names == {p for p, r in owners.items() if r['writer'] == area}, f'{area} file set mismatch')
        for name in names:
            checked(dest/name, pins[name]['sha256'])
            (base/name).parent.mkdir(parents=True, exist_ok=True)
            (dest/name).rename(base/name)
    return base
