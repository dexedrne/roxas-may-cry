"""Resolve authenticated whole files, retail record members and archive spans."""
import csv
import hashlib
import json
from functools import lru_cache
from pathlib import Path
import shutil
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'upstream'
RECIPES = ROOT / 'patches/standalone'
sys.path.insert(0, str(UPSTREAM / 'tools'))
import egs_extract as egs


def rows(path):
    with path.open(encoding='utf-8', newline='') as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


class Unavailable(Exception):
    """A developer extract lacks a retail payload."""


def verify(data, pin, source):
    if hashlib.sha256(data).hexdigest() != pin:
        raise ValueError(f'SHA-256 mismatch at {source}')
    return data


def extract_clean(packages, clean):
    for row in rows(UPSTREAM / 'patches/models-ui/packages.tsv'):
        package = row['package']
        egs.extract(SimpleNamespace(hed=packages / (package + '.hed'),
                                    pkg=packages / (package + '.pkg'),
                                    names=UPSTREAM / 'docs/needed-names.txt',
                                    out=clean / package, list_found=False))


class Sources:
    def __init__(self, tmp, packages, clean, loose):
        self.tmp = tmp
        self.packages = packages
        self.clean = clean
        self.loose = loose
        self.sources = {r['sha256']: r for r in rows(RECIPES / 'sources.tsv')}
        self.alternates = {}
        for row in rows(RECIPES / 'record-alternates.tsv'):
            self.alternates.setdefault(row['sha256'], []).append(row)
        self.pins = json.loads((UPSTREAM / 'config/package-index.json').read_text())
        self.previous = None
        self.used = {}

    @lru_cache(maxsize=None)
    def index(self, package):
        name = package + '.hed'
        with (self.packages / name).open('rb') as stream:
            if name in self.pins:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != self.pins[name]:
                    raise ValueError(f'unsupported package revision {name}')
                stream.seek(0)
            return egs.read_index(stream)

    def record(self, locator, pin, loose, size=None):
        package, md5, leaf = locator.split('/', 2)
        if self.packages is None:
            if not loose or not self.loose or not (self.loose / loose).is_file():
                raise Unavailable(locator)
            data = self.payload(self.loose / loose, pin, size)
            self.used[pin] = dict(record=locator, loose=loose)
            return data
        key = bytes.fromhex(md5)
        directory = self.tmp / 'record'
        if self.previous != (package, key):
            if directory.exists():
                shutil.rmtree(directory)
            directory.mkdir()
            pkg = self.packages / (package + '.pkg')
            with pkg.open('rb') as data, (directory / 'manifest.tsv').open('w', newline='') as log:
                egs.extract_record(data, pkg.stat().st_size, key, self.index(package)[key],
                                   md5, directory, csv.writer(log), [package+'.hed', pkg.name])
            self.previous = package, key
        path = directory / ('remastered/' + md5 + '/' + leaf if leaf else 'original/' + md5)
        data = self.payload(path, pin, size)
        self.used[pin] = dict(record=locator)
        return data

    @staticmethod
    def payload(path, pin, size):
        data = path.read_bytes()
        # Some uncompressed EGS HD entries include their final 16-byte alignment.
        if size is not None and len(data) == ((size+15) & ~15):
            data = data[:size]
        return verify(data, pin, path)

    def texture(self, row):
        from standalone_textures import bgra, bc3
        archive = row['archive']
        file = self.clean / archive
        if file.is_file():
            blob = verify(file.read_bytes(), row['source_sha256'], archive)
            self.used[row['source_sha256']] = dict(extract=archive)
        else:
            pkg, rel = archive.split('/', 1)
            loc = '/'.join((pkg, row['record_md5'], row['member']))
            blob = self.record(loc, row['source_sha256'], rel.removeprefix('original/'))
        if row['slot']:
            return bgra(blob, int(row['slot']), int(row['material']), int(row['levels']))
        return bc3(blob)

    @lru_cache(maxsize=3)
    def resolve(self, sha):
        r = self.sources[sha]
        loc = r['locator']
        if r['method'] == 'clean-file':
            data = (self.clean / loc).read_bytes()
            self.used[sha] = dict(extract=loc)
        elif r['method'] == 'retail-span':
            blob = self.record(loc, r['parent_sha256'], r['loose'])
            off = int(r['offset'])
            data = blob[off:off+int(r['bytes'])]
        elif r['method'] == 'aligned-png':
            data = self.record(loc, r['parent_sha256'], r['loose'], int(r['bytes']))
            data += bytes([0xcd]) * (-len(data) % 16)
        elif r['method'] == 'collection-record':
            for witness in (r, *self.alternates.get(sha, [])):
                package = witness['locator'].split('/', 1)[0]
                if self.packages and not (self.packages / (package + '.hed')).is_file():
                    continue
                try:
                    data = self.record(witness['locator'], sha, witness['loose'], int(r['bytes']))
                    break
                except Unavailable:
                    continue
            else:
                if self.packages:
                    raise ValueError(f'missing collection package for {loc}')
                raise Unavailable(loc)
        else:
            raise ValueError(f'unknown source method {r["method"]}')
        return verify(data, sha, loc)
