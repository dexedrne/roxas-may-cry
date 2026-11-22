"""Build a full PC/US mod or a private diagnostic overlay."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'upstream/tools'), str(ROOT / 'upstream/tools/rooms')]
from formats import bar, directory_record, require, selected
from records import encode, pack, tokens
from message_pool import pack_messages, read_messages, word


def sha(data):
    return hashlib.sha256(data).hexdigest()


def checked(root, path, digest):
    data = (root / path).read_bytes()
    require(sha(data) == digest, f'input digest mismatch: {path}')
    return data


def wording(text):
    return b''.join(word(c) if c not in '.\n' else bytes([2 if c == '\n' else 0x4f]) for c in text)


def messages(data, specs):
    p, size = struct.unpack_from('<II', data, 24)
    require(struct.unpack_from('<HH4s', data, 16) == (2, 0, b'sys\0') and
            struct.unpack_from('<HH4s', data, 32) == (24, 0, b'md_m') and
            struct.unpack_from('<I', data, 44)[0] == 0 and p + size == len(data), 'unexpected message BAR')
    pool = read_messages(data[p:])
    for s in specs:
        old = pool[s['id']]
        needle = wording(s['old'])
        require(sha(old) == s['old_sha256'] and old.count(needle) == 1, 'message mismatch')
        pool[s['id']] = old.replace(needle, wording(s['new']))
    payload = pack_messages(pool, False)
    result = bytearray(data[:p]) + payload
    directory_record(result, 0, p, len(payload))
    directory_record(result, 1, (len(result) + 15) & ~15, 0)
    return result


def scene_props(rows, spec):
    index = spec['read_assets']
    tag, payload = rows[index]
    require(tag == 'event36', 'missing scene preload list')
    payload = bytearray(payload)
    count, = struct.unpack_from('<H', payload)
    at = 8
    changes = []
    props = dict(spec['actors'])
    for _ in range(count):
        op, size = struct.unpack_from('<HH', payload, at)
        require(size >= 6 and at + size <= len(payload), 'invalid scene asset')
        actor, = struct.unpack_from('<H', payload, at + 4)
        if op in (37, 56) and actor in props:
            changes.append((op, actor))
            struct.pack_into('<H', payload, at + 4, props[actor])
        at += size
    require(at == len(payload) and sorted(changes) == sorted((o,a) for a in props for o in (37,56)), 'unexpected prop loads')
    rows[index] = tag, payload
    for index, (actor, replacement) in zip(spec['set_actors'], spec['actors'], strict=True):
        tag, payload = rows[index]
        require(tag == 'event1' and payload.startswith(pack('H', actor)), 'scene actor mismatch')
        rows[index] = tag, pack('H', replacement) + payload[2:]


def room(data, spec):
    rows = tokens(spec['kind'], data)
    if spec['op'] == 'remove-entities':
        tag, group = rows[1]
        old = spec['old_count']
        require(tag == 'group' and struct.unpack_from('<h', group, 4)[0] == old, 'group count mismatch')
        for index, actor in reversed(spec['entities']):
            tag, entity = rows[index]
            require(2 <= index < 2 + old and tag == 'entity' and entity.startswith(pack('I', actor)), 'party slot mismatch')
            del rows[index]
        rows[1] = 'group', group[:4] + pack('h', old - len(spec['entities'])) + group[6:]
    elif spec['op'] == 'scene-props':
        scene_props(rows, spec)
    else:
        raise ValueError(f'unknown room operation {spec["op"]}')
    result = encode(spec['kind'], rows)
    tokens(spec['kind'], result)
    return result


def treasure(data, spec):
    version, count = struct.unpack_from('<HH', data)
    codec = struct.Struct('<HH4Bhh')
    rows = list(codec.iter_unpack(data[4:]))
    require(count == len(rows) == spec['old_count'], 'treasure count mismatch')
    for row in spec['rows']:
        require([r for r in rows if r[2:6] == tuple(row[2:6])] == spec['old'], 'chest already exists')
        require(all(r[0] != row[0] and r[-1] != row[-1] for r in rows), 'treasure ID collision')
        rows.append(tuple(row))
    return pack('HH', version, len(rows)) + b''.join(codec.pack(*row) for row in rows)


def archive(data, spec):
    entries = bar(data)
    entry = selected(entries, spec['ordinal'], spec['kind'], spec['name'], spec['link'])
    original = entry.data(data)
    replacement = treasure(original, spec) if spec['op'] == 'treasure' else room(original, spec)
    end = entry.offset + entry.size
    result = bytearray(data[:entry.offset] + replacement + data[end:])
    delta = len(replacement) - entry.size
    for e in entries:
        if e == entry:
            directory_record(result, e.ordinal, e.offset, len(replacement))
        elif e.offset >= end:
            directory_record(result, e.ordinal, e.offset + delta, e.size)
        else:
            require(e.offset + e.size <= entry.offset, 'shared member requires a dedicated writer')
    bar(result)
    return result


def build(base, out, *, complete=False, game_files=None, native=None):
    specs = {}
    for path in sorted((ROOT / 'patches/rmc').glob('*.json')):
        for spec in json.loads(path.read_text()):
            for key in ('path', 'source', 'mirror'):
                if key in spec:
                    name = spec[key]
                    require('\\' not in name and ':' not in name and
                            all(part not in ('', '.', '..') for part in name.split('/')), f'unsafe path: {name}')
            specs.setdefault(spec['path'], []).append(spec)
    require(not out.exists(), 'choose a fresh output directory')
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out.parent) as tmp:
        stage = Path(tmp) / 'patch'
        installed = stage / 'installed'
        manifest = {}
        for path, edits in sorted(specs.items()):
            s = edits[0]
            names = [path]
            if s['op'] == 'copy':
                require(len(edits) == 1 and s['old'] is None and not (base / path).exists(), f'expected a new destination: {path}')
                source = base if (base / s['source']).is_file() else game_files
                require(source is not None, f'missing arena source: {s["source"]}')
                result = checked(source, s['source'], s['sha256'])
            else:
                data = checked(base, path, s['sha256'])
                if s['op'] == 'wording':
                    require(all((e['ordinal'], e['kind'], e['name'], e['link']) == (0, 2, 'sys', 0)
                                for e in edits), 'unexpected message member selector')
                    checked(base, s['mirror'], s['sha256'])
                    require(all(e['sha256'] == s['sha256'] and e['mirror'] == s['mirror'] for e in edits), 'inconsistent message pins')
                    result = messages(data, edits)
                    names.append(s['mirror'])
                else:
                    writers = []
                    for edit in edits:
                        require(edit['sha256'] == s['sha256'], f'inconsistent archive pins: {path}')
                        if edit['op'] == 'require-treasure':
                            table = selected(bar(data), edit['ordinal'], edit['kind'], edit['name'], edit['link']).data(data)
                            rows = list(struct.iter_unpack('<HH4Bhh', table[4:]))
                            for row in edit['rows']:
                                require(rows.count(tuple(row)) == 1, 'missing page chest')
                        else:
                            writers.append(edit)
                    require(len(writers) == 1, 'expected one archive writer')
                    result = archive(data, writers[0])
            for name in names:
                require(name not in manifest, f'duplicate patch target: {name}')
                dest = installed / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(result)
                manifest[name] = sha(result)
        pins = dict(manifest)
        if complete:
            for file in sorted(base.rglob('*')):
                if not file.is_file():
                    continue
                name = file.relative_to(base).as_posix()
                if name not in pins:
                    dest = installed / name
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(file, dest)
                    pins[name] = sha(file.read_bytes())
            if native:
                for name in ('rmc-native.dll', 'rmc.ini'):
                    dest = installed / 'dll' / name
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(native / name, dest)
                    pins['dll/' + name] = manifest['dll/' + name] = sha(dest.read_bytes())
        (stage / 'changed.json').write_text(json.dumps(manifest, indent=2) + '\n')
        paths = set(pins)
        actual = {p.relative_to(installed).as_posix() for p in installed.rglob('*') if p.is_file()}
        require(paths == actual, 'installed file set mismatch')
        for name, digest in pins.items():
            checked(installed, name, digest)
        provenance = {
            'version': '0.1.0', 'kind': 'full' if complete else 'diagnostic overlay',
            'source_allowlist_sha256': (ROOT/'upstream/source.sha256').read_text().split()[0],
            'patches': {p.name: sha(p.read_bytes()) for p in sorted((ROOT/'patches/rmc').glob('*.json'))},
            'installed': dict(sorted(pins.items())),
        }
        (stage / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        require(len(paths) == len({n.casefold() for n in paths}), 'case collision in copy destinations')
        assets = [dict(name=n, method='copy', source=[dict(name='installed/' + n)]) for n in sorted(paths)]
        (stage / 'mod.yml').write_text(json.dumps(dict(title='Roxas May Cry', version='0.1.0', game='kh2', assets=assets), indent=2) + '\n')
        stage.rename(out)
    print(f'{len(manifest)} changed files listed in {out}/changed.json')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--vanilla', type=Path, help='six extracted PC/US packages with their manifests')
    mode.add_argument('--baseline', type=Path, help='build a diagnostic overlay against an installed test tree')
    parser.add_argument('--assets', type=Path, help='private asset inputs independently extracted or rebuilt from your games')
    parser.add_argument('--game-files', type=Path, help='loose arena assets extracted from the game')
    parser.add_argument('--native', type=Path, help='directory containing rmc-native.dll and rmc.ini')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        output = args.out.resolve()
        require(not output.exists(), 'choose a fresh output directory')
        for source in (args.vanilla, args.baseline, args.assets, args.game_files, args.native):
            if source:
                require(not output.is_relative_to(source.resolve()), 'output overlaps input')
        if args.baseline:
            require(args.native is None and args.assets is None, 'the diagnostic overlay accepts data only')
            build(args.baseline, output, game_files=args.game_files)
        else:
            from baseline import source_assets, reconstruct
            output.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(dir=output.parent) as tmp:
                work = Path(tmp)
                clean = args.vanilla.resolve()
                base = reconstruct(clean, source_assets(clean, work, args.assets), work)
                build(base, output, complete=True, game_files=args.game_files, native=args.native)
    except (ValueError, OSError, struct.error, subprocess.CalledProcessError) as exc:
        parser.exit(1, f'build: {exc}\n')
