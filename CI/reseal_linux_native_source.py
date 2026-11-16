#!/usr/bin/env python3
"""Freeze reviewed source after overlay integration, before a clean native build.

This maintenance command changes the manifest and runner's expected digest. It is
not called during validation and is not proof that an edited source was reviewed.
The package records its final combined patch digest separately, avoiding cycles.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex

INFRASTRUCTURE = {
    'CI/run_linux_native_ai.py', 'CI/test_linux_native_ai.py',
    'CI/linux_native_source_manifest.json', 'CI/reseal_linux_native_source.py',
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def reseal(source, overlay):
    source = source.resolve(strict=True)
    overlay = overlay.resolve(strict=True)
    if overlay.is_relative_to(source):
        raise ValueError('AI overlay patch must be outside the source tree')
    patch = overlay.read_text(encoding='utf-8')
    for line in patch.splitlines():
        paths = []
        if line.startswith(('--- ', '+++ ')):
            paths = [line[4:].split('\t', 1)[0]]
        elif line.startswith('diff --git '):
            paths = shlex.split(line[len('diff --git '):])
            if len(paths) != 2:
                raise ValueError('Malformed Git diff header')
        elif line.startswith(('rename from ', 'rename to ', 'copy from ', 'copy to ')):
            paths = [line.split(' ', 2)[2]]
        for name in paths:
            if name.startswith(('a/', 'b/')):
                name = name[2:]
            if name in INFRASTRUCTURE:
                raise ValueError('AI-only overlay must exclude Linux runner infrastructure')
    runner = source / 'CI/run_linux_native_ai.py'
    runner_text = runner.read_text(encoding='utf-8')
    commits = re.findall(r'^SOURCE_COMMIT = "([0-9a-f]{40})"$', runner_text, re.MULTILINE)
    if len(commits) != 1:
        raise ValueError('Runner must identify exactly one pinned base commit')
    files = {}
    for path in sorted(source.rglob('*')):
        name = path.relative_to(source).as_posix()
        if path.is_symlink():
            raise ValueError('Symlink in source: ' + name)
        if not path.is_file() or name in INFRASTRUCTURE:
            continue
        if path.parent == source / 'CI/__pycache__' and path.suffix == '.pyc':
            continue
        if any(part in {'.git', 'CMakeFiles', 'build', 'out'} for part in path.relative_to(source).parts):
            raise ValueError('Use a clean source tree, without build output or Git internals: ' + name)
        files[name] = digest(path)
    if not files or 'CMakeLists.txt' not in files or 'test/googletest/CMakeLists.txt' not in files:
        raise ValueError('Complete engine and googletest source required')
    manifest = {'base_commit': commits[0], 'ai_overlay_patch_sha256': digest(overlay),
                'overlay_digest_scope': 'AI-only overlay excluding Linux runner infrastructure; not final combined patch',
                'files': files}
    encoded = json.dumps(manifest, sort_keys=True, indent=2) + '\n'
    manifest_hash = hashlib.sha256(encoded.encode()).hexdigest()
    updated, count = re.subn(r'^SOURCE_MANIFEST_SHA256 = "[0-9a-f]{64}"$',
                            'SOURCE_MANIFEST_SHA256 = "' + manifest_hash + '"', runner_text,
                            flags=re.MULTILINE)
    if count != 1:
        raise ValueError('Runner must contain exactly one source manifest digest')
    (source / 'CI/linux_native_source_manifest.json').write_text(encoded, encoding='utf-8')
    runner.write_text(updated, encoding='utf-8')
    return {'manifest_sha256': manifest_hash, 'verified_file_count': len(files),
            'ai_overlay_patch_sha256': manifest['ai_overlay_patch_sha256']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', required=True, type=Path)
    parser.add_argument('--ai-overlay-patch', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(reseal(args.source_dir, args.ai_overlay_patch), indent=2))


if __name__ == '__main__':
    main()
