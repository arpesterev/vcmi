#!/usr/bin/env python3
"""Validate and rehydrate a trusted, branch-scoped incremental build cache.

A cache is not a successful test result. Always configure/build/test after restore.
Only this checkout's byte-identical tracked inputs get their original timestamps;
changed/new inputs are newer than every restored output. No permissive fallback.
Run exclusively: no compiler or source writer may run concurrently with this tool.
The caller fingerprints compiler, SDK, dependencies, options and runner image.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import subprocess
import sys
import tempfile
import time

SCHEMA = 2


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def safe_path(root, name):
    rel = PurePosixPath(name)
    if not name or name == '.' or rel.is_absolute() or '..' in rel.parts or str(rel) != name:
        raise ValueError('Unsafe cached path: ' + repr(name))
    path = root.joinpath(*rel.parts)
    for parent in list(path.parents)[:len(rel.parts) - 1]:
        if parent.is_symlink():
            raise ValueError('Symlink ancestor: ' + str(parent))
    return path


def describe(path):
    info = path.lstat()
    if stat.S_ISREG(info.st_mode):
        return {'kind': 'file', 'sha256': digest(path), 'size': info.st_size,
                'mtime_ns': info.st_mtime_ns, 'mode': stat.S_IMODE(info.st_mode)}
    if stat.S_ISLNK(info.st_mode):
        return {'kind': 'symlink', 'target': os.readlink(path),
                'mtime_ns': info.st_mtime_ns, 'mode': stat.S_IMODE(info.st_mode)}
    raise ValueError('Unsupported file type: ' + str(path))


def tracked(root):
    data = subprocess.check_output(['git', '-C', str(root), 'ls-files',
                                    '--cached', '--recurse-submodules', '-z'])
    names = data.decode('utf-8').split('\0')
    result = {}
    for name in names:
        if not name:
            continue
        path = safe_path(root, name)
        # Deleted tracked files are intentionally absent; CMake reruns afterwards.
        if path.exists() or path.is_symlink():
            result[name] = describe(path)
    return result


def tree(root, state):
    result = {}
    for folder, directories, filenames in os.walk(root, followlinks=False):
        for name in list(directories):
            if (Path(folder) / name).is_symlink():
                filenames.append(name)
                directories.remove(name)
        for name in filenames:
            path = Path(folder) / name
            if path == state:
                continue
            rel = path.relative_to(root).as_posix()
            safe_path(root, rel)
            result[rel] = describe(path)
    return result


def content(entry):
    return {k: v for k, v in entry.items() if k != 'mtime_ns'}


def validate_external_roots(roots, build):
    roots = [Path(root).absolute() for root in roots]
    if len(roots) != len(set(roots)):
        raise ValueError('Duplicate external dependency root')
    for root in roots:
        if not root.is_dir() or root.is_symlink() or root.resolve() != root:
            raise ValueError('Missing/non-canonical external dependency root: ' + str(root))
        if root == build or build in root.parents or root in build.parents:
            raise ValueError('External dependency root overlaps build directory')
    return sorted(roots)


def identity(source, build, fingerprint, external_roots=()):
    return {'schema': SCHEMA, 'source_root': str(source), 'build_root': str(build),
            'fingerprint': json.loads(fingerprint.read_text()),
            'external_roots': [str(root) for root in external_roots]}


def snapshot(source, build, state, fingerprint, external_roots=()):
    if not build.is_dir() or build.is_symlink():
        raise ValueError('Build directory must exist and not be a symlink')
    external_roots = validate_external_roots(external_roots, build)
    value = identity(source, build, fingerprint, external_roots)
    value['sources'] = tracked(source)
    value['build_files'] = tree(build, state)
    value['external_files'] = {str(root): tree(root, state) for root in external_roots}
    if not value['build_files']:
        raise ValueError('Refusing empty build cache')
    state.parent.mkdir(parents=True, exist_ok=True)
    if state.parent.resolve() != state.parent or state.is_symlink():
        raise ValueError('Symlink metadata path rejected')
    with tempfile.NamedTemporaryFile(mode='w', dir=state.parent, prefix=state.name + '.', delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n')
    temporary.replace(state)
    return {'operation': 'snapshot', 'sources': len(value['sources']),
            'build_files': len(value['build_files']),
            'external_files': sum(len(files) for files in value['external_files'].values())}


def restore(source, build, state, fingerprint, external_roots=()):
    if state.parent.resolve() != state.parent or not state.is_file() or state.is_symlink():
        raise ValueError('Required build cache metadata missing; use explicit bootstrap')
    if not build.is_dir() or build.is_symlink():
        raise ValueError('Required build directory missing or symlinked')
    saved = json.loads(state.read_text())
    external_roots = validate_external_roots(external_roots, build)
    expected = identity(source, build, fingerprint, external_roots)
    for key, value in expected.items():
        if saved.get(key) != value:
            raise ValueError('Incompatible cache ' + key + '; explicit bootstrap required')
    for section, root in [('sources', source), ('build_files', build)]:
        if not isinstance(saved.get(section), dict):
            raise ValueError('Missing cache section ' + section)
        for name, entry in saved[section].items():
            safe_path(root, name)
            if not isinstance(entry, dict) or not isinstance(entry.get('mtime_ns'), int) or entry['mtime_ns'] < 0:
                raise ValueError('Invalid cached timestamp')
    actual_build = tree(build, state)
    if not actual_build or set(actual_build) != set(saved['build_files']):
        raise ValueError('Build file inventory mismatch; explicit bootstrap required')
    for name, entry in actual_build.items():
        if content(entry) != content(saved['build_files'][name]):
            raise ValueError('Build file content/mode mismatch: ' + name)
    if not isinstance(saved.get('external_files'), dict) or set(saved['external_files']) != {str(root) for root in external_roots}:
        raise ValueError('External dependency root inventory mismatch')
    for root in external_roots:
        files = saved['external_files'][str(root)]
        if not isinstance(files, dict):
            raise ValueError('Invalid external dependency inventory')
        for name, entry in files.items():
            safe_path(root, name)
            if not isinstance(entry, dict) or not isinstance(entry.get('mtime_ns'), int) or entry['mtime_ns'] < 0:
                raise ValueError('Invalid external dependency timestamp')
        actual = tree(root, state)
        if set(actual) != set(files) or any(content(entry) != content(files[name]) for name, entry in actual.items()):
            raise ValueError('External dependency content/inventory mismatch: ' + str(root))
    actual_sources = tracked(source)
    unchanged = [name for name, entry in actual_sources.items()
                 if name in saved['sources'] and content(entry) == content(saved['sources'][name])]
    changed = sorted(set(actual_sources) - set(unchanged))
    removed = sorted(set(saved['sources']) - set(actual_sources))
    max_stamp = max(entry['mtime_ns'] for entry in saved['build_files'].values())
    # A future-dated cache can otherwise suppress rebuilds indefinitely.
    if max_stamp > time.time_ns() + 2_000_000_000:
        raise ValueError('Future-dated output cache; explicit bootstrap required')
    # Ninja v7 records command-start time using filesystem-clock precision.
    # time.time_ns() can be slightly ahead of that clock, causing another build
    # even after a successful compile. Sample the filesystem, never wall time.
    changed_stamp = 0
    if changed:
        deadline = time.monotonic() + 4
        while True:
            with tempfile.NamedTemporaryFile(dir=build, prefix='.mtime-probe-') as probe:
                probe.write(b'probe')
                probe.flush()
                changed_stamp = os.fstat(probe.fileno()).st_mtime_ns
            if changed_stamp > max_stamp:
                break
            if time.monotonic() >= deadline:
                raise ValueError('Filesystem clock is not newer than cache; bootstrap required')
            time.sleep(0.02)
    # Everything was validated before any timestamps are changed.
    for root in external_roots:
        for name, entry in saved['external_files'][str(root)].items():
            path = safe_path(root, name)
            os.utime(path, ns=(path.lstat().st_atime_ns, entry['mtime_ns']), follow_symlinks=False)
    for name, entry in saved['build_files'].items():
        path = safe_path(build, name)
        os.utime(path, ns=(path.lstat().st_atime_ns, entry['mtime_ns']), follow_symlinks=False)
    for name in unchanged:
        path = safe_path(source, name)
        os.utime(path, ns=(path.lstat().st_atime_ns, saved['sources'][name]['mtime_ns']), follow_symlinks=False)
    for name in changed:
        path = safe_path(source, name)
        os.utime(path, ns=(path.lstat().st_atime_ns, changed_stamp), follow_symlinks=False)
    return {'operation': 'restore', 'unchanged_sources': len(unchanged),
            'changed_sources': changed, 'removed_sources': removed,
            'verified_build_files': len(actual_build),
            'verified_external_files': sum(len(files) for files in saved['external_files'].values())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['snapshot', 'restore'])
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--fingerprint', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--external-root', action='append', type=Path, default=[],
                        help='Additional dependency tree to verify and restore timestamps (repeatable)')
    args = parser.parse_args()
    try:
        # Do not silently resolve away a caller-supplied symlink root.
        for root in (args.source_root.absolute(), args.build_dir.absolute()):
            if root.is_symlink() or root.resolve() != root:
                raise ValueError('Symlink/non-canonical root rejected: ' + str(root))
        result = globals()[args.operation](args.source_root.absolute(), args.build_dir.absolute(),
                                           args.state.absolute(), args.fingerprint.absolute(), args.external_root)
    except (ValueError, OSError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print('INCREMENTAL_CACHE_ERROR: ' + str(error), file=sys.stderr)
        return 2
    text = json.dumps(result, sort_keys=True, indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text)
    print(text, end='')
    return 0


if __name__ == '__main__':
    sys.exit(main())
