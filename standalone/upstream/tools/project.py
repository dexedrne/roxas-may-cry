"""Pinned catalogs and workspace paths shared by the three commands."""
import csv
import hashlib
import json
import os
from pathlib import Path
from formats import require

ROOT = Path(__file__).resolve().parents[1]
MODEL = 'obj/M_EX760_RAW.mdlx'
DATA = ('00worldpoint.bin','00battle.bin','ard/us/eh05.ard')
PRIVATE = Path(os.environ.get('RMC_WORK', ROOT/'private'))


def digest(data):
    return hashlib.sha256(data).hexdigest()


def recipe(name):
    return json.loads((ROOT/'patches'/f'{name}.yml').read_text())


def catalog(file):
    if file == 'docs/inventory/baseline.tsv':
        pin = json.loads((ROOT/'docs/inventory/pins.json').read_text())
        checked(ROOT/file,pin['baseline_sha256'])
    with (ROOT/file).open() as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    seen = set()
    for row in rows:
        name = row['path']
        require(name.casefold() not in seen and '\\' not in name and
                all(s not in ('','.','..') for s in name.split('/')), f'invalid or duplicate path {name}')
        seen.add(name.casefold())
    return {row['path']:row for row in rows}


def inventory():
    pin = json.loads((ROOT/'docs/inventory/pins.json').read_text())
    require(digest((ROOT/'INVENTORY-52.tsv').read_bytes()) == pin['inventory_sha256'], 'inventory fingerprint')
    return catalog('INVENTORY-52.tsv')


def checked(path, sha):
    data = path.read_bytes()
    require(digest(data) == sha, f'SHA-256 mismatch: {path}')
    return data


def destination(path):
    path = path.resolve()
    require(PRIVATE.resolve() in path.parents, f'choose a destination below private/: {path}')
    return path


def check_manifest(root):
    pin = json.loads((ROOT/'docs/inventory/pins.json').read_text())
    checked(root/'mod.yml',pin['manifest_sha256'])

def package_manifest(path):
    """Authenticate portable manifest fields, including both source package names."""
    with path.open(encoding='utf-8', newline='') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        fields = ['path', 'size', 'sha256', 'hed', 'pkg', 'record_sha256']
        require(reader.fieldnames == fields, f'SHA-256 mismatch: {path} (manifest header)')
        records = list(reader)
    canonical = []
    for row in records:
        for key in ('hed', 'pkg'):
            row[key] = row[key].replace(chr(92), '/').rsplit('/', 1)[-1]
        canonical.append('\t'.join(row[k] for k in fields)+'\n')
    return records, digest(''.join(canonical).encode())
