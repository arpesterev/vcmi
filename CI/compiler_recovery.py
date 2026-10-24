#!/usr/bin/env python3
"""Fail-closed validation for supplementary Linux ccache-only recovery.

This is not a build cache or a cold-build fallback. The workflow must first
successfully validate its normal build/dependency caches with incremental_cache
restore, then call prepare, restore the exact-identity supplementary cache, and
call check before any compilation. Call bound only after compilers have stopped.
Never run concurrently with a cache writer. Only ccache's completed, self-contained
entries and narrowly validated housekeeping files may be persisted.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys


PROFILE = 'linux-gcc-test'
MAX_BYTES = 768 * 1024**2
SCHEMA = 1
HEX = re.compile(r'[0-9a-f]{64}\Z')
# ccache 4: first two digest bytes are hexadecimal, remaining 18 are base32hex.
ENTRY_NAME = re.compile(r'[0-9a-f]{4}[0-9a-v]{29}[MR]\Z')
IDENTITY_STRINGS = (
    'profile', 'workspace', 'home', 'os', 'arch', 'runner_image', 'cmake',
    'ninja', 'bootstrap_compiler', 'dependency_commit', 'presets_sha256',
    'build_contract_sha256', 'cache_protocol_sha256', 'dependency_setup_sha256',
    'onnx_setup_sha256', 'ccache', 'ccache_sha256', 'onnxruntime_sha256',
    'compiler', 'compiler_sha256', 'packages',
)


def canonical(path):
    path = Path(path).absolute()
    if path.resolve() != path or path.is_symlink():
        raise ValueError('Symlink/non-canonical path rejected: ' + str(path))
    return path


def regular(path):
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        raise ValueError('Only single-link regular files are allowed: ' + str(path))
    return info


def sha256(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError('Duplicate JSON key: ' + key)
        value[key] = item
    return value


def read_json(path):
    path = canonical(path)
    if regular(path).st_size > 4 * 1024**2:
        raise ValueError('Evidence file is unexpectedly large')
    raw = path.read_bytes()
    value = json.loads(raw, object_pairs_hook=unique_object)
    if not isinstance(value, dict):
        raise ValueError('Evidence must be a JSON object')
    return value, raw


def prerequisites(identity_path, validation_path):
    identity, raw = read_json(identity_path)
    if type(identity.get('schema')) is not int or identity['schema'] != 1:
        raise ValueError('Unsupported installed identity schema')
    for key in IDENTITY_STRINGS:
        if not isinstance(identity.get(key), str) or not identity[key].strip():
            raise ValueError('Incomplete installed identity: ' + key)
        if key.endswith('_sha256') and not HEX.fullmatch(identity[key]):
            raise ValueError('Invalid installed identity digest: ' + key)
    if (identity['profile'] != PROFILE or identity['os'] != 'Linux'
            or identity['runner_image'] == 'unknown'
            or identity['home'] != str(canonical(Path.home()))
            or identity['workspace'] != str(canonical(Path.cwd()))
            or not re.fullmatch(r'[0-9a-f]{40}', identity['dependency_commit'])):
        raise ValueError('Installed identity does not match the current Linux profile/paths')
    validation, _ = read_json(validation_path)
    if validation.get('operation') != 'restore':
        raise ValueError('A successful strict cache restore is required; no cold fallback')
    for name in ('verified_build_files', 'verified_external_files', 'unchanged_sources'):
        if type(validation.get(name)) is not int or validation[name] < 0:
            raise ValueError('Incomplete cache restore evidence: ' + name)
    if validation['verified_build_files'] == 0:
        raise ValueError('No verified build files; no cold fallback')
    for name in ('changed_sources', 'removed_sources'):
        if (not isinstance(validation.get(name), list)
                or any(not isinstance(item, str) for item in validation[name])):
            raise ValueError('Incomplete cache restore evidence: ' + name)
    return identity, hashlib.sha256(raw).hexdigest()


def small_text(path, limit=16 * 1024):
    if regular(path).st_size > limit:
        raise ValueError('Oversized ccache metadata: ' + str(path))
    return path.read_text(encoding='ascii')


def validate_config(path):
    values = {}
    for line in small_text(path).splitlines():
        if not line.strip():
            continue
        key, sep, value = line.partition('=')
        key, value = key.strip(), value.strip()
        if not sep or key in values:
            raise ValueError('Invalid/duplicate ccache configuration')
        values[key] = value
    if (set(values) != {'compiler_check', 'sloppiness', 'max_size'}
            or values['compiler_check'] != 'content' or values['sloppiness'] != ''
            or values['max_size'] not in {'768M', '768.0M', '768 MB', '768.0 MB',
                                          '768MiB', '768.0MiB', '768 MiB', '768.0 MiB'}):
        raise ValueError('Only the existing bounded, content-checked ccache configuration is allowed')


def validate_tag(path):
    lines = small_text(path, 1024).splitlines()
    comments = {
        '', 'This file is a cache directory tag created by ccache.',
        'For information about cache directory tags, see:',
        'http://www.brynosaurus.com/cachedir/', 'https://www.brynosaurus.com/cachedir/',
        'http://bford.info/cachedir/', 'https://bford.info/cachedir/',
    }
    if (not lines or lines[0] != 'Signature: 8a477f597d28d172789f06886806bc55'
            or any(not line.startswith('#') or line[1:].strip() not in comments
                   for line in lines[1:])):
        raise ValueError('Unexpected ccache directory tag content')


def inventory(root, enforce_size=True):
    """Validate the complete tree before invoking ccache, including on failure."""
    def fail_walk(error):
        raise error

    root = canonical(root)
    expected = canonical(Path.home() / '.cache' / 'vcmi-incremental' / PROFILE)
    if root != expected or not root.is_dir():
        raise ValueError('Only the existing profile ccache directory is allowed')
    total, count, entries = 0, 0, {}
    for folder, directories, files in os.walk(root, followlinks=False, onerror=fail_walk):
        for name in directories + files:
            path = Path(folder) / name
            parts = path.relative_to(root).parts
            info = path.lstat()
            if stat.S_ISDIR(info.st_mode):
                if parts in [('tmp',), ('lock',)] or (
                        1 <= len(parts) <= 4
                        and all(re.fullmatch(r'[0-9a-f]', part) for part in parts)):
                    continue
                raise ValueError('Unexpected cache directory: ' + str(path))
            regular(path)  # Reject symlinks, hardlinks, sockets, devices and FIFOs.
            total += info.st_size
            count += 1
            if enforce_size and total > MAX_BYTES:
                raise ValueError('Compiler recovery cache exceeds 768 MiB')
            if parts == ('ccache.conf',):
                validate_config(path)
            elif parts == ('tmp', '.cleaned') and info.st_size == 0:
                pass
            elif (len(parts) in (2, 3) and parts[-1] == 'stats'
                  and all(re.fullmatch(r'[0-9a-f]', part) for part in parts[:-1])):
                if not re.fullmatch(r'(?:[0-9]+\n)+', small_text(path)):
                    raise ValueError('Unexpected ccache statistics content')
            elif len(parts) == 2 and parts[-1] == 'CACHEDIR.TAG' and re.fullmatch(r'[0-9a-f]', parts[0]):
                validate_tag(path)
            elif (3 <= len(parts) <= 5
                  and all(re.fullmatch(r'[0-9a-f]', part) for part in parts[:-1])
                  and ENTRY_NAME.fullmatch(''.join(parts))):
                if info.st_size == 0 or info.st_size > MAX_BYTES:
                    raise ValueError('Invalid/oversized ccache entry')
                entries[path.relative_to(root).as_posix()] = path
            else:
                raise ValueError('Non-ccache or unfinished content rejected: ' + str(path))
    if not (root / 'ccache.conf').is_file():
        raise ValueError('Required ccache configuration missing')
    return {'logical_bytes': total, 'files': count}, entries


def ccache_command(identity, root):
    executable = shutil.which('ccache')
    if not executable or sha256(Path(executable)) != identity['ccache_sha256']:
        raise ValueError('Installed ccache binary does not match the verified identity')
    # Do not inherit configuration overrides or read a system configuration file.
    environment = {key: value for key, value in os.environ.items() if not key.startswith('CCACHE_')}
    environment.update(CCACHE_DIR=str(root), CCACHE_CONFIGPATH=str(root / 'ccache.conf'))
    return executable, environment


def inspect_entries(entries, executable, environment):
    results = {}
    for name, path in sorted(entries.items()):
        inspected = subprocess.run([executable, '--inspect', str(path)], check=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, env=environment, timeout=15).stdout
        kind = '0 (result)' if name.endswith('R') else '1 (manifest)'
        if ('Entry type: ' + kind not in inspected.splitlines()
                or 'Self-contained: yes' not in inspected.splitlines()):
            raise ValueError('Only complete self-contained ccache entries are allowed')
        if name.endswith('R'):
            # Use the content key, not its current directory depth or statistics.
            results[name.replace('/', '')] = sha256(path)
    return results


def recover(operation, identity_path, validation_path, baseline=None):
    identity, fingerprint = prerequisites(identity_path, validation_path)
    root = canonical(Path.home() / '.cache' / 'vcmi-incremental' / PROFILE)
    result = {'schema': SCHEMA, 'operation': operation, 'profile': PROFILE,
              'fingerprint': fingerprint, 'cache_dir': str(root), 'max_bytes': MAX_BYTES}
    if operation == 'prepare':
        return result
    if operation not in ('check', 'bound'):
        raise ValueError('Unknown compiler recovery operation')
    old = None
    if baseline is not None:
        old, _ = read_json(baseline)
        if (old.get('schema') != SCHEMA or old.get('operation') != 'check'
                or old.get('fingerprint') != fingerprint or old.get('cache_dir') != str(root)
                or not isinstance(old.get('result_entries'), dict)
                or any(not ENTRY_NAME.fullmatch(name) or not name.endswith('R')
                       or not isinstance(value, str) or not HEX.fullmatch(value)
                       for name, value in old['result_entries'].items())):
            raise ValueError('Baseline must be a matching post-restore check report')
    details, entries = inventory(root, enforce_size=operation != 'bound')
    executable, environment = ccache_command(identity, root)
    # Inspect before cleanup too: invalid content must not be silently discarded.
    results = inspect_entries(entries, executable, environment)
    if operation == 'bound':
        subprocess.run([executable, '--max-size=768M', '--cleanup'], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                       env=environment, timeout=60)
        details, entries = inventory(root)
        results = inspect_entries(entries, executable, environment)
    result.update(details, result_entries=results)
    if old is not None:
        changed = sum(old['result_entries'].get(name) != value for name, value in results.items())
        result.update(new_or_changed_results=changed, save_needed=changed > 0)
    else:
        result['save_needed'] = False
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['prepare', 'check', 'bound'])
    parser.add_argument('--identity', type=Path, required=True)
    parser.add_argument('--validation', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--github-output', type=Path)
    args = parser.parse_args(argv)
    try:
        result = recover(args.operation, args.identity, args.validation, args.baseline)
        encoded = json.dumps(result, sort_keys=True, indent=2) + '\n'
        if args.report:
            canonical(args.report).write_text(encoded)
        if args.github_output:
            with canonical(args.github_output).open('a') as stream:
                stream.write('fingerprint=' + result['fingerprint'] + '\n')
                stream.write('save-needed=' + str(result.get('save_needed', False)).lower() + '\n')
        print(encoded, end='')
        return 0
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print('COMPILER_RECOVERY_ERROR: ' + str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
