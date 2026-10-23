#!/usr/bin/env python3
"""Validate exact mandatory production regression cases in a full GoogleTest XML."""
import argparse
import json
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--manifest', required=True)
parser.add_argument('--xml', required=True)
args = parser.parse_args()
manifest = json.loads(Path(args.manifest).read_text())
assert manifest['schema_version'] == 1
required = manifest['required_tests']
assert required and len(required) == len(set(required)), 'Empty or duplicate required test names'
root = ET.parse(args.xml).getroot()
assert int(root.get('failures', '-1')) == 0 and int(root.get('errors', '0')) == 0
cases = root.findall('.//testcase')
identities = Counter(case.get('classname', '') + '.' + case.get('name', '') for case in cases)
assert all(count == 1 for count in identities.values()), 'Duplicate test identity'
by_name = {case.get('classname', '') + '.' + case.get('name', ''): case for case in cases}
for name in required:
    assert name in by_name, 'Missing compiled regression: ' + name
    case = by_name[name]
    assert case.get('status') == 'run', 'Regression did not run: ' + name
    assert case.find('skipped') is None and case.find('failure') is None, 'Regression failed/skipped: ' + name
executed = [case for case in cases if case.get('status') == 'run']
assert len(executed) >= manifest['minimum_executed_tests']
assert all(case.find('skipped') is None and case.find('failure') is None for case in executed)
print(f'COMPLETE: {len(required)} required production regressions; {len(executed)} total executed tests')
