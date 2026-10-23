#!/usr/bin/env python3
"""Read-only inventory diagnostics. Never repair or accept an invalid cache."""
import argparse
import json
from pathlib import Path
from incremental_cache import content, tree, validate_external_roots


def differences(saved, actual):
    return {'missing': sorted(saved.keys() - actual.keys()),
            'added': sorted(actual.keys() - saved.keys()),
            'changed': {name: {'saved': content(saved[name]), 'actual': content(actual[name])}
                        for name in sorted(saved.keys() & actual.keys()) if content(saved[name]) != content(actual[name])}}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--external-root', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    saved = json.loads(args.state.read_text())
    report = {}
    for root in validate_external_roots(args.external_root, args.build.resolve()):
        delta = differences(saved.get('external_files', {}).get(str(root), {}), tree(root, args.state.resolve()))
        report[str(root)] = delta
        print(str(root), {key: len(value) for key, value in delta.items()})
        for key, value in delta.items():
            print(key, json.dumps(dict(list(value.items())[:20]) if isinstance(value, dict) else value[:20]))
    args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
