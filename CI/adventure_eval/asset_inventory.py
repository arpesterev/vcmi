"""Read-only render-resource preflight; presence is necessary, not sufficiency."""
from pathlib import Path
import struct

REQUIRED = ('help.txt', 'tentcolr.txt', 'players.pal', 'default.def', 'notoserif-medium.ttf', 'ah00_.def')


def inspect(roots):
    names = set()
    files, archives, errors = [], [], []
    for root in roots:
        if not root.is_dir():
            continue
        for path in sorted(root.rglob('*')):
            if not path.is_file():
                continue
            name = path.name.lower()
            files.append({'path': str(path), 'size': path.stat().st_size})
            names.add(name)
            if path.suffix.lower() not in ('.lod', '.pac'):
                continue
            entries = []
            try:
                size = path.stat().st_size
                with path.open('rb') as stream:
                    header = stream.read(92)
                    # Engine deliberately ignores fake empty LOD files in test data.
                    if len(header) < 92:
                        archives.append({'path': str(path), 'entries': [], 'empty_or_placeholder': True})
                        continue
                    count = struct.unpack_from('<I', header, 8)[0]
                    if count > (size - 92) // 32:
                        raise ValueError('LOD directory exceeds file size')
                    for _ in range(count):
                        entry = stream.read(32)
                        raw_name, offset, full_size, _, compressed_size = struct.unpack('<16sIIII', entry)
                        member = raw_name.split(b'\0', 1)[0].decode('ascii').lower()
                        if offset + (compressed_size or full_size) > size:
                            raise ValueError('LOD member exceeds archive size')
                        if full_size:
                            entries.append(member)
                            names.add(member)
                archives.append({'path': str(path), 'entries': entries})
            except (OSError, ValueError, UnicodeError) as error:
                errors.append({'path': str(path), 'error': str(error)})
    missing = sorted(set(REQUIRED) - names)
    return {'required_sentinels': REQUIRED, 'missing': missing, 'files': files,
            'archives': archives, 'errors': errors,
            'preflight_passed': not missing and not errors,
            'meaning': 'Presence only, not proof of complete or redistributable gameplay assets.'}


if __name__ == '__main__':
    import argparse
    import json
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, action='append', required=True,
                        help='Verified public extracted data or packaged resources; repeatable')
    parser.add_argument('--output', type=Path, required=True)
    arguments = parser.parse_args()
    result = inspect([root.resolve() for root in arguments.root])
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(result, indent=2))
    print(json.dumps({key: result[key] for key in ('preflight_passed', 'missing', 'errors')}))
    # Inventory collection itself succeeds even when graphics must remain blocked.
    # Consumers MUST inspect preflight_passed; it is not an engine-test pass code.
