#!/usr/bin/env python3
"""Exact-identity, bounded macOS ccache 4.14 recovery, never a build-cache fallback.

The Linux helper is deliberately byte-stable. Load a private, digest-pinned copy
of its tree/config/binary validators; only the Mac identity and ccache 4.14 entry
names/types differ. Call after strict build AND Conan cache restore, before any
compilation, and again only after all compilers have stopped.
"""
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import types


BASE_SHA256 = 'f998c4b75212618cd20d095f36ffc5abe9bfe42aa68be6fdf87e1649096463b2'
PROFILE = 'macos-arm-conan-ninja-release'
# ccache 4.14 uses a 160-bit hexadecimal key, without the former M/R suffix.
# https://github.com/ccache/ccache/blob/v4.14/src/ccache/storage/local/localstorage.cpp
ENTRY_NAME = re.compile(r'[0-9a-f]{40}\Z')
# Native 4.14 on APFS creates a disposable mmap index in tmp. Never persist it
# or let restored inode/hash associations reach a compiler on a different runner.
# https://github.com/ccache/ccache/blob/v4.14/src/ccache/inodecache.cpp
INODE_CACHE_NAME = 'inode-cache-64.v3'
# Darwin arm64 ABI: uint32 version + padding + three atomic<int64> counters;
# 32768 buckets of atomic<pid_t> plus four (20-byte key, 20-byte hash, int) entries.
INODE_CACHE_BYTES = 32 + 32768 * (4 + 4 * (20 + 20 + 4))
IDENTITY_STRINGS = (
    'profile', 'workspace', 'home', 'os', 'arch', 'runner_image', 'cmake',
    'ninja', 'bootstrap_compiler', 'dependency_commit', 'presets_sha256',
    'build_contract_sha256', 'cache_protocol_sha256', 'ccache', 'ccache_sha256',
    'compiler_sha256', 'xcode', 'sdk', 'sdk_path', 'dependency_archive_sha256',
    'conan_version', 'conan_packages_sha256', 'conan_generated_sha256',
)


def load_core(path):
    source = path.read_bytes()
    if hashlib.sha256(source).hexdigest() != BASE_SHA256:
        raise ValueError('Compiler recovery base helper changed; explicit review required')
    core = types.ModuleType('_macos_compiler_recovery_core')
    # Execute exactly the verified bytes, not a second read or a cached .pyc.
    exec(compile(source, str(path), 'exec'), core.__dict__)
    core.PROFILE = PROFILE
    core.ENTRY_NAME = ENTRY_NAME
    return core


core = load_core(Path(__file__).with_name('compiler_recovery.py'))


def prerequisites(identity_path, validation_path):
    identity, raw = core.read_json(identity_path)
    if type(identity.get('schema')) is not int or identity['schema'] != 1:
        raise ValueError('Unsupported installed identity schema')
    for key in IDENTITY_STRINGS:
        if not isinstance(identity.get(key), str) or not identity[key].strip():
            raise ValueError('Incomplete installed Mac identity: ' + key)
        if key.endswith('_sha256') and not core.HEX.fullmatch(identity[key]):
            raise ValueError('Invalid installed identity digest: ' + key)
    workspace = core.canonical(Path.cwd())
    if (identity['profile'] != PROFILE or identity['os'] != 'Darwin'
            or identity['arch'] != 'arm64' or identity['runner_image'] == 'unknown'
            or identity['home'] != str(core.canonical(Path.home()))
            or identity['workspace'] != str(workspace)
            or not re.fullmatch(r'[0-9a-f]{40}', identity['dependency_commit'])
            or not Path(identity['sdk_path']).is_absolute()
            or identity['ccache'].splitlines()[0] != 'ccache version 4.14'):
        raise ValueError('Installed identity does not match the current Mac profile/paths/format')
    recipes = identity.get('conanfile_sha256')
    if (not isinstance(recipes, dict) or not recipes
            or str(workspace / 'conanfile.py') not in recipes
            or any(Path(name).parent != workspace
                   or not (Path(name).match('conanfile.*') or Path(name).match('conan*.lock'))
                   or not isinstance(value, str) or not core.HEX.fullmatch(value)
                   for name, value in recipes.items())):
        raise ValueError('Complete installed Conan recipe identity is required')
    validation, _ = core.read_json(validation_path)
    if validation.get('operation') != 'restore':
        raise ValueError('A successful strict cache restore is required; no cold fallback')
    for name in ('verified_build_files', 'verified_external_files', 'unchanged_sources'):
        if type(validation.get(name)) is not int or validation[name] < 0:
            raise ValueError('Incomplete cache restore evidence: ' + name)
    if not validation['verified_build_files'] or not validation['verified_external_files']:
        raise ValueError('Verified build and Conan files required; no cold fallback')
    for name in ('changed_sources', 'removed_sources'):
        if (not isinstance(validation.get(name), list)
                or any(not isinstance(item, str) for item in validation[name])):
            raise ValueError('Incomplete cache restore evidence: ' + name)
    return identity, hashlib.sha256(raw).hexdigest()


def inspect_entries(entries, executable, environment):
    results = {}
    for name, path in sorted(entries.items()):
        inspected = subprocess.run([executable, '--inspect', str(path)], check=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, env=environment, timeout=15).stdout.splitlines()
        kinds = [line for line in inspected if line.startswith('Entry type: ')]
        if (kinds not in [['Entry type: 0 (result)'], ['Entry type: 1 (manifest)']]
                or inspected.count('Self-contained: yes') != 1
                or any(line.startswith('Self-contained: ') and line != 'Self-contained: yes'
                       for line in inspected)):
            raise ValueError('Only complete self-contained ccache entries are allowed')
        if kinds == ['Entry type: 0 (result)']:
            results[name.replace('/', '')] = core.sha256(path)
    return results


def discard_inode_cache(root):
    """Discard only the known 64-bit v3 index, with all compilers stopped.

    Its contents are never trusted or loaded by ccache. All other temporary
    files remain subject to the unchanged pinned inventory's strict rejection.
    """
    root = core.canonical(root)
    expected = core.canonical(Path.home() / '.cache' / 'vcmi-incremental' / PROFILE)
    if root != expected or not root.is_dir():
        raise ValueError('Only the existing profile ccache directory is allowed')
    path = root / 'tmp' / INODE_CACHE_NAME
    # Check every ancestor even if the file itself is absent.
    core.canonical(path)
    try:
        info = core.regular(path)
    except FileNotFoundError:
        return 0
    if info.st_uid != core.os.getuid() or info.st_size != INODE_CACHE_BYTES:
        raise ValueError('Unexpected temporary ccache inode index owner/size')
    with path.open('rb') as stream:
        if stream.read(4) != b'\x03\x00\x00\x00':
            raise ValueError('Unexpected temporary ccache inode index version')
    path.unlink()
    return info.st_size


def recover(operation, identity_path, validation_path, baseline=None):
    identity, fingerprint = prerequisites(identity_path, validation_path)
    root = core.canonical(Path.home() / '.cache' / 'vcmi-incremental' / PROFILE)
    result = {'schema': core.SCHEMA, 'operation': operation, 'profile': PROFILE,
              'fingerprint': fingerprint, 'cache_dir': str(root), 'max_bytes': core.MAX_BYTES}
    if operation == 'prepare':
        return result
    if operation not in ('check', 'bound'):
        raise ValueError('Unknown compiler recovery operation')
    old = None
    if baseline is not None:
        old, _ = core.read_json(baseline)
        if (type(old.get('schema')) is not int or old['schema'] != core.SCHEMA
                or old.get('operation') != 'check' or old.get('profile') != PROFILE
                or old.get('fingerprint') != fingerprint or old.get('cache_dir') != str(root)
                or old.get('max_bytes') != core.MAX_BYTES
                or not isinstance(old.get('result_entries'), dict)
                or any(not ENTRY_NAME.fullmatch(name)
                       or not isinstance(value, str) or not core.HEX.fullmatch(value)
                       for name, value in old['result_entries'].items())):
            raise ValueError('Baseline must be a matching post-restore check report')
    discarded = discard_inode_cache(root)
    details, entries = core.inventory(root, enforce_size=operation != 'bound')
    executable, environment = core.ccache_command(identity, root)
    results = inspect_entries(entries, executable, environment)
    if operation == 'bound':
        subprocess.run([executable, '--max-size=768M', '--cleanup'], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                       env=environment, timeout=60)
        discarded += discard_inode_cache(root)
        details, entries = core.inventory(root)
        results = inspect_entries(entries, executable, environment)
    result.update(details, result_entries=results, discarded_inode_cache_bytes=discarded)
    if old is not None:
        changed = sum(old['result_entries'].get(name) != value for name, value in results.items())
        result.update(new_or_changed_results=changed, save_needed=changed > 0)
    else:
        result['save_needed'] = False
    return result


# Reuse the pinned CLI/report/output error handling only inside this private module.
core.recover = recover
core.__doc__ = __doc__
main = core.main


if __name__ == '__main__':
    sys.exit(main())
