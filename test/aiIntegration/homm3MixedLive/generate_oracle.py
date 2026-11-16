#!/usr/bin/env python3
"""Extract additional live caller/defense bodies from the pinned source checkout."""
import hashlib
import json
from pathlib import Path
import sys

PIN = 'e12906accf96c2662b1abadbc49ddc111991bf21'
root = Path(sys.argv[1])
out = Path(__file__).parent

def extract(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

entries = [
    ('ai.cpp', 'unsigned char combatManager::chooseDefenseHex('),
    ('ai.cpp', 'unsigned char combatManager::attemptShooterDefense('),
    ('ai.cpp', 'void combatManager::doCompAI('),
    ('findpath.cpp', 'long searchArray::getTravelTime('),
]
parts, manifest = [], []
for file, signature in entries:
    source = (root / file).read_text()
    original = extract(source, signature)
    adapted = original.replace('pathCell* cell = getHex(hex);', 'const pathCell* cell = getHex(hex);')
    manifest.append(dict(file=file, signature=signature,
        file_sha256=hashlib.sha256((root / file).read_bytes()).hexdigest(),
        body_sha256=hashlib.sha256(original.encode()).hexdigest(),
        adapted_sha256=hashlib.sha256(adapted.encode()).hexdigest()))
    parts.append(adapted)
(out / 'mixed-bodies.pinned.inc').write_text('\n\n'.join(parts) + '\n')
(out / 'source-manifest.json').write_text(json.dumps(dict(pin=PIN, functions=manifest), indent=2) + '\n')
