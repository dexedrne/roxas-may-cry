"""Build the D1 missions-messages scope and verify it against the pinned installation."""
import argparse
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from formats import require
from project import catalog, destination, inventory


def owned():
    ledger = inventory()
    paths = sorted(p for p, row in catalog('docs/inventory/baseline.tsv').items()
                   if row['writer'] == 'missions-messages')
    require(all(p in ledger for p in paths) and len(paths) == 135, 'unexpected D1 writer scope')
    return paths


def invoke(script, *args):
    subprocess.run([sys.executable, '-B', script, *args], check=True)


def run(args):
    paths = owned()
    cache = destination(args.cache) if args.mode == 'import' else args.cache
    if args.mode == 'import':
        for script in ('missions.py', 'messages.py'):
            invoke(HERE/script, 'import', '--reference', args.reference, '--cache', cache)
        return
    out = destination(args.out)
    require(not out.exists(), f'choose a fresh output directory: {out}')
    out.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=out.parent, prefix='.missions-messages-') as temp:
        stage = Path(temp)/'installed'
        stage.mkdir()
        for script in ('missions.py', 'messages.py'):
            invoke(HERE/script, 'build', '--clean', args.clean, '--cache', cache, '--out', stage)
        require(sorted(p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file()) == paths,
                'generated paths differ from D1 writer scope')
        stage.rename(out)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(required=True, dest='mode')
    for action in ('import', 'build'):
        options = actions.add_parser(action)
        for name in {'import': ('reference','cache'), 'build': ('clean','cache','out')}[action]:
            options.add_argument('--'+name, type=Path, required=True)
    try:
        run(parser.parse_args())
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f'missions-messages: {exc}\n')
