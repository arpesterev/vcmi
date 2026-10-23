#!/usr/bin/env python3
"""Read-only diagnostic, never a build-cache acceptance or migration mechanism."""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess


def sha256(path):
    result = hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key: ' + key)
        result[key] = value
    return result


def read_json(path):
    return json.loads(pathlib.Path(path).read_text(), object_pairs_hook=unique_object)


def packages(value):
    result = {}
    for line in value.splitlines():
        name, separator, version = line.partition('=')
        if not separator or not name or not version or name in result:
            raise ValueError('Invalid or duplicate package record: ' + line)
        result[name] = version
    if not result:
        raise ValueError('Empty package inventory')
    return result


def compare(baseline, current, expected_image):
    """Compare every field exactly; only the known ImageVersion is observational."""
    for label, value in [('baseline', baseline), ('current', current)]:
        if not isinstance(value, dict):
            raise ValueError(label + ': identity must be an object')
        if type(value.get('schema')) is not int or value['schema'] != 1:
            raise ValueError(label + ': unsupported identity schema')
        if not re.fullmatch(r'\d{8}\.\d+\.\d+', str(value.get('runner_image', ''))):
            raise ValueError(label + ': missing or unknown ImageVersion')
        if value.get('profile') != 'linux-gcc-test' or value.get('os') != 'Linux':
            raise ValueError(label + ': unexpected platform/profile')
        if 'packages' in value:
            packages(value['packages'])
    if current['runner_image'] != expected_image:
        raise ValueError('Unreviewed diagnostic image: ' + current['runner_image'])
    differences = []
    for key in sorted(set(baseline) | set(current)):
        if key == 'runner_image':
            continue
        if (key not in baseline or key not in current or
                type(baseline[key]) is not type(current[key]) or baseline[key] != current[key]):
            differences.append({
                'field': key,
                'baseline_present': key in baseline,
                'current_present': key in current,
                'baseline': baseline.get(key),
                'current': current.get(key),
            })
    package_changes = []
    if 'packages' in baseline and 'packages' in current:
        old, new = packages(baseline['packages']), packages(current['packages'])
        package_changes = [
            {'package': name, 'baseline': old.get(name), 'current': new.get(name)}
            for name in sorted(set(old) | set(new)) if old.get(name) != new.get(name)
        ]
    return {
        'diagnostic_only': True,
        'cache_reuse_authorized': False,
        'equal_except_runner_image': not differences,
        'runner_image': {'baseline': baseline['runner_image'], 'current': current['runner_image']},
        'differing_fields': [item['field'] for item in differences],
        'differences': differences,
        'package_changes': package_changes,
        'package_inventory_sha256': {
            label: hashlib.sha256(value['packages'].encode()).hexdigest()
            for label, value in [('baseline', baseline), ('current', current)] if 'packages' in value
        },
    }


def command(*args):
    return subprocess.check_output(args, text=True, timeout=30).strip()


def binary_record(path):
    requested = pathlib.Path(path)
    resolved = requested.resolve(strict=True)
    # Hash only explicitly selected toolchain executables/libraries, never HOME,
    # /root, general filesystem trees, credentials, or arbitrary compiler output.
    if not any(resolved.is_relative_to(pathlib.Path(root)) for root in ('/usr', '/opt')):
        raise ValueError('Toolchain path outside the allowed system roots: ' + str(resolved))
    if not resolved.is_file() or resolved.stat().st_size > 256 * 1024**2:
        raise ValueError('Unexpected binary type/size: ' + str(resolved))
    return {'path': str(requested), 'resolved_path': str(resolved),
            'bytes': resolved.stat().st_size, 'sha256': sha256(resolved)}


def inventory(cache_root):
    """Additional evidence, not proof against an absent historic binary inventory."""
    root = pathlib.Path(cache_root)
    if root != pathlib.Path('.ci/apt-cache') or root.is_symlink() or not root.is_dir():
        raise ValueError('Expected a regular workspace .ci/apt-cache directory')
    binaries = {}
    for name in ('gcc-14', 'g++-14', 'g++', 'ccache', 'cmake', 'ninja', 'ld', 'as', 'ar', 'ranlib'):
        path = shutil.which(name)
        if not path:
            raise ValueError('Required tool missing: ' + name)
        binaries[name] = binary_record(path)
    for label, compiler, option in (
        ('cc1', 'gcc-14', '-print-prog-name=cc1'),
        ('cc1plus', 'g++-14', '-print-prog-name=cc1plus'),
        ('libstdc++', 'g++-14', '-print-file-name=libstdc++.so'),
        ('libgcc_s', 'gcc-14', '-print-file-name=libgcc_s.so.1'),
    ):
        binaries[label] = binary_record(command(compiler, option))
    archives = []
    total = 0
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError('Unexpected dependency-cache symlink: ' + str(path))
        if not path.is_file():
            continue
        total += path.stat().st_size
        if total > 3 * 1024**3:
            raise ValueError('Dependency downloads exceed 3 GiB')
        record = {'path': str(path), 'bytes': path.stat().st_size, 'sha256': sha256(path)}
        if path.suffix == '.deb':
            record['package_metadata'] = command(
                'dpkg-deb', '--show', '--showformat=${Package}=${Version} ${Architecture}\n', str(path))
        archives.append(record)
    installed = command('dpkg-query', '-W', '-f=${binary:Package}=${Version}\n')
    return {'diagnostic_only': True, 'cache_reuse_authorized': False,
            'binaries': binaries, 'downloaded_archives': archives,
            'downloaded_bytes': total,
            'installed_package_inventory_sha256': hashlib.sha256(installed.encode()).hexdigest(),
            'installed_package_count': len(packages(installed)),
            'inventory_hash_encoding': 'dpkg-query text, stripped, UTF-8, without trailing newline',
            'limitation': 'No historic hashes exist for additional binaries or .deb archives; '
                          'this evidence alone cannot certify cache compatibility.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest='operation', required=True)
    comparison = subparsers.add_parser('compare')
    comparison.add_argument('--baseline', required=True)
    comparison.add_argument('--current', required=True)
    comparison.add_argument('--expected-image', required=True)
    comparison.add_argument('--output', required=True)
    evidence = subparsers.add_parser('inventory')
    evidence.add_argument('--cache-root', default='.ci/apt-cache')
    evidence.add_argument('--output', required=True)
    args = parser.parse_args()
    if args.operation == 'compare':
        report = compare(read_json(args.baseline), read_json(args.current), args.expected_image)
        report['baseline_file_sha256'] = sha256(args.baseline)
        report['current_file_sha256'] = sha256(args.current)
    else:
        report = inventory(args.cache_root)
    pathlib.Path(args.output).write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')
    if args.operation == 'compare':
        print('Differing fields (ImageVersion is observational in this diagnostic only):',
              ', '.join(report['differing_fields']) or '(none)')
        for change in report['package_changes']:
            print('Package:', change['package'], repr(change['baseline']), '->', repr(change['current']))
        print('This diagnostic never authorizes build-cache reuse or migration.')
        return 0 if report['equal_except_runner_image'] else 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
