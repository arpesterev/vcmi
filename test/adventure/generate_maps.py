#!/usr/bin/env python3
"""Generate public synthetic native maps, never load external/user saves."""
import argparse
import json
from pathlib import Path
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
sys.path.insert(0, str(args.repo / 'CI/adventure_eval'))
from run import generate
args.output.mkdir(parents=True, exist_ok=True)
evidence = {}
for name in ('productive_move', 'stable_goal_progress', 'blocked_goal_alternative'):
    evidence[name] = generate(args.repo, args.output / (name + '.vmap'), name)
(args.output / 'provenance.json').write_text(json.dumps(evidence, indent=2, sort_keys=True) + '\n')
print(json.dumps({'status': 'generated_unexecuted', 'maps': evidence}, sort_keys=True))
