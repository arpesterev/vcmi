#!/usr/bin/env python3
"""Check expanded Ninja commands, not CMake's version-dependent rule templates."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess


def validate(commands):
    compiled = 0
    for line in commands.splitlines():
        words = shlex.split(line)
        if '-c' not in words or '-o' not in words:
            continue
        compiled += 1
        compile_end = words.index('-c')
        if not any(Path(word).name == 'ccache' for word in words[:compile_end]):
            raise ValueError('C/C++ compilation bypasses ccache: ' + line)
    if not compiled:
        raise ValueError('No compiler commands found in selected Ninja target graph')
    return compiled


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', required=True)
    parser.add_argument('--evidence', required=True)
    parser.add_argument('targets', nargs='+')
    args = parser.parse_args()
    commands = subprocess.check_output(['ninja', '-C', args.build, '-t', 'commands', *args.targets], text=True)
    evidence = Path(args.evidence)
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / 'expanded-build-commands.txt').write_text(commands)
    count = validate(commands)
    (evidence / 'launcher-check.json').write_text(json.dumps({'compiler_commands': count, 'all_use_ccache': True}) + '\n')
    print(f'Validated ccache launcher on {count} expanded compiler commands')


if __name__ == '__main__':
    main()
