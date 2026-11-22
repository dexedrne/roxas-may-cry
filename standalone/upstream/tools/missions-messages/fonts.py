"""Retain the two complete clean US font archives, including their padding."""
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from formats import require
from project import ROOT, checked, destination, inventory


def build(clean, out):
    ledger = inventory()
    with (ROOT/'patches/missions-messages/fonts.tsv').open() as stream:
        for row in csv.DictReader(stream, delimiter='\t'):
            path = row['path']
            require(row['operation'] == 'retain-file' and row['sha256'] == ledger[path]['sha256'],
                    f'font recipe mismatch for {path}')
            payload = checked(clean / row['package'] / row['source'], row['sha256'])
            target = destination(out/path)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)
