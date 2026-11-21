"""Compare every local ZIP member against an independent owner-built release."""
import argparse
import csv
import hashlib
from pathlib import Path
import zipfile


def contents(path):
    rows={}
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            if info.filename in rows:
                raise ValueError(f'duplicate member: {info.filename}')
            with archive.open(info) as stream:
                sha=hashlib.sha256()
                for block in iter(lambda:stream.read(1024*1024),b''):
                    sha.update(block)
                rows[info.filename]=sha.hexdigest()
    return rows


def differences(reference,built):
    return {name:(reference.get(name,'missing'),built.get(name,'missing'))
            for name in sorted(reference.keys()|built.keys()) if reference.get(name)!=built.get(name)}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference',type=Path)
    parser.add_argument('built',type=Path)
    parser.add_argument('--profile',choices=['standard','full'],required=True)
    args=parser.parse_args()
    reference,built=contents(args.reference),contents(args.built)
    identity = hashlib.sha256(b''.join(
        name.encode() + b'\0' + bytes.fromhex(pin) for name,pin in sorted(reference.items())
    )).hexdigest()
    if identity != "5bb83ec4c5757f7284ffa8e5102707227cfe9827682f4978135973b9d0a3825e":
        parser.exit(1,'unsupported reference ZIP member inventory\n')
    actual=differences(reference,built)
    with (Path(__file__).resolve().parents[1]/'tests/release-differences.tsv').open(encoding='utf-8') as f:
        expected={r['path']:(r['reference'],r[args.profile]) for r in csv.DictReader(f,delimiter='\t')
                  if r['reference']!=r[args.profile]}
    for name in sorted(actual.keys()|expected.keys()):
        if actual.get(name)!=expected.get(name):
            parser.exit(1,f'unexpected result: {name}\n')
    print(f'{len(reference)-len(actual)} matching entries, {len(actual)} verified differences')
