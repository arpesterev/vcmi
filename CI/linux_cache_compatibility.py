#!/usr/bin/env python3
"""Certify one reviewed Linux image alias, without weakening cache validation.

The raw identity is never edited. Only an exact, type-preserving match to a
byte-pinned baseline can produce a separate canonical identity. This helper is
not a general image migration mechanism and has no image/baseline override CLI.
Runner/dependency certification is preliminary: callers must also certify the
installed toolchain before validating or reusing compiled outputs.
"""
import argparse
import hashlib
import json
import os
import pathlib
import sys
import tempfile

BASELINE_DIRECTORY = pathlib.Path(__file__).resolve().parent / 'linux-diagnostic-baseline'
BASELINE_IMAGE = '20261012.334.1'
APPROVED_IMAGE = '20261023.334.3'
BASELINE_SHA256 = {
    'runner': 'e5f3db8a3395709a5a60c47b55d56a59910fca4ad7b24500a0ca18f13bf6dcb1',
    'dependency': 'aeb32a29af25fe67dfefd742c9cee60e32a493abb60c71d9d74705db61d9b132',
    'toolchain': 'e923c33704e900860b291feb4ad31b5d3049137100f6511e561f74809d233b8d',
}
EVIDENCE_PROVENANCE = {
    'repository': 'arpesterev/vcmi',
    'run_id': '40092736702',
    'commit': '4356d0a99a1894520e8dfa1455e5c99dc3ce5f8c',
    'artifact_id': '12560010876',
    'artifact_zip_sha256': '309b8daeb4e6d3af10cb15eeccc15b686fea9a97e0e5a285b11ed233ed88c1f4',
    'installed_identity_sha256': 'acbe7fbe607ac3818d5d51eb55c5188cd5d97599727db1348b41a6ff03972c65',
    'installed_package_inventory_sha256': 'ec8787c0714c3ecf0cd2eb3b828fb3f7d9c8c3290da4dfe3a929b6f7ace2c2d8',
    'finding': 'All installed identity fields are exactly equal except runner_image.',
    'scope': 'Only the two named images and the exact pinned identity for each phase.',
    'limitation': 'Certifies equivalence of the existing identity fingerprint, not whole-OS '
                  'byte identity. Historic hashes for additional binaries are unavailable.',
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key: ' + key)
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError('Nonstandard JSON constant: ' + value)


def parse_identity(data):
    value = json.loads(data.decode('utf-8'), object_pairs_hook=unique_object,
                       parse_constant=reject_constant)
    if type(value) is not dict:
        raise ValueError('Identity must be a JSON object')
    return value


def typed_equal(left, right):
    """Unlike Python dict equality, True and 1 (or 1.0) are not equivalent."""
    if type(left) is not type(right):
        return False
    if type(left) is dict:
        return left.keys() == right.keys() and all(typed_equal(left[k], right[k]) for k in left)
    if type(left) is list:
        return len(left) == len(right) and all(typed_equal(a, b) for a, b in zip(left, right))
    return left == right


def certify(phase, observed_bytes, baseline_directory=BASELINE_DIRECTORY):
    """Return (canonical bytes or None, audit report); never alter input bytes."""
    if phase not in BASELINE_SHA256:
        raise ValueError('Unknown identity phase: ' + str(phase))
    report = {
        'schema': 1,
        'phase': phase,
        'status': 'rejected',
        'accepted': False,
        'alias_applied': False,
        'actual_image': None,
        'canonical_image': None,
        'observed_sha256': sha256(observed_bytes),
        'canonical_sha256': None,
        'expected_baseline_sha256': BASELINE_SHA256[phase],
        'baseline_sha256': None,
        'approved_image_pair': [BASELINE_IMAGE, APPROVED_IMAGE],
        'differing_fields': [],
        'evidence_provenance': dict(EVIDENCE_PROVENANCE),
        'scope': 'installed-toolchain' if phase == 'toolchain' else 'pre-install-only',
        'requires_installed_toolchain_check': phase != 'toolchain',
    }
    try:
        observed = parse_identity(observed_bytes)
        report['actual_image'] = observed.get('runner_image')
        baseline_bytes = (pathlib.Path(baseline_directory) / (phase + '-identity.json')).read_bytes()
        report['baseline_sha256'] = sha256(baseline_bytes)
        if report['baseline_sha256'] != BASELINE_SHA256[phase]:
            raise ValueError('Baseline byte hash does not match the reviewed pin')
        baseline = parse_identity(baseline_bytes)
        if baseline.get('runner_image') != BASELINE_IMAGE:
            raise ValueError('Baseline image does not match the reviewed pin')
        actual_image = observed.get('runner_image')
        if type(actual_image) is not str or actual_image not in (BASELINE_IMAGE, APPROVED_IMAGE):
            raise ValueError('Missing, mistyped, or unreviewed runner_image')
        candidate = dict(observed)
        candidate['runner_image'] = BASELINE_IMAGE
        differences = [key for key in sorted(baseline.keys() | candidate.keys())
                       if key not in baseline or key not in candidate
                       or not typed_equal(baseline[key], candidate[key])]
        report['differing_fields'] = differences
        if differences:
            raise ValueError('Identity differs from the reviewed baseline: ' + ', '.join(differences))
        # The exact dictionary check above preserves every field, including the
        # complete package inventory and build configuration, without filtering.
        report.update({
            'status': 'accepted',
            'accepted': True,
            'alias_applied': actual_image == APPROVED_IMAGE,
            'canonical_image': BASELINE_IMAGE,
            'canonical_sha256': sha256(baseline_bytes),
            'reason': ('Exact approved image alias; all other fields match with their types'
                       if actual_image == APPROVED_IMAGE else 'Exact pinned baseline identity'),
        })
        return baseline_bytes, report
    except (ValueError, UnicodeError, OSError) as error:
        report['reason'] = str(error)
        return None, report


def same_path(left, right):
    if left.resolve() == right.resolve():
        return True
    return left.exists() and right.exists() and left.samefile(right)


def validate_paths(observed, output, report):
    """Refuse input/baseline overwrites, including symlink and hardlink aliases."""
    protected = [observed] + list(BASELINE_DIRECTORY.glob('*-identity.json'))
    for destination in (output, report):
        if destination.is_symlink() or (destination.exists() and not destination.is_file()):
            raise ValueError('Destination must be a regular file: ' + str(destination))
        if any(same_path(destination, source) for source in protected):
            raise ValueError('Destination would overwrite raw identity or baseline: ' + str(destination))
    if same_path(output, report):
        raise ValueError('Canonical output and report must have separate paths')


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.' + path.name + '.', delete=False) as stream:
            temporary = pathlib.Path(stream.name)
            stream.write(data)
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=tuple(BASELINE_SHA256))
    parser.add_argument('--observed', required=True, type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    parser.add_argument('--report', required=True, type=pathlib.Path)
    args = parser.parse_args(argv)
    try:
        validate_paths(args.observed, args.output, args.report)
        # Never leave a stale previously accepted identity after a rejection.
        args.output.unlink(missing_ok=True)
        args.report.unlink(missing_ok=True)
        observed_bytes = args.observed.read_bytes()
        canonical_bytes, report = certify(args.phase, observed_bytes)
        report['observed_path'] = str(args.observed)
        report['canonical_path'] = str(args.output)
        if canonical_bytes is not None:
            atomic_write(args.output, canonical_bytes)
        try:
            atomic_write(args.report, (json.dumps(report, sort_keys=True, indent=2) + '\n').encode('utf-8'))
        except OSError:
            args.output.unlink(missing_ok=True)
            raise
        if canonical_bytes is None:
            print('Linux cache compatibility rejected: ' + report['reason'], file=sys.stderr)
            return 1
        print(report['canonical_sha256'])
        return 0
    except (ValueError, OSError) as error:
        print('Linux cache compatibility failed: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
