#!/usr/bin/env python3
"""Record actual Ninja edges, not an assumed cache-hit or source-directory filter."""
import argparse, collections, json, pathlib, re

def entries(path):
    result = {}
    if path.exists():
        for line in path.read_text().splitlines():
            if line.startswith('#'): continue
            parts = line.split('\t')
            if len(parts) == 5:
                result[parts[3]] = parts
    return result

def main():
    p = argparse.ArgumentParser(); p.add_argument('--build', type=pathlib.Path, required=True); p.add_argument('--evidence', type=pathlib.Path, required=True); a = p.parse_args()
    before = entries(a.evidence/'ninja-before.log'); after = entries(a.evidence/'ninja-after.log')
    changed = [name for name, row in after.items() if before.get(name) != row]
    objects = sorted(name for name in changed if name.endswith(('.o','.obj')))
    groups = collections.Counter(re.search(r'(?:^|/)CMakeFiles/([^/]+)\.dir/', name).group(1) if re.search(r'(?:^|/)CMakeFiles/([^/]+)\.dir/', name) else 'other' for name in objects)
    noop = (a.evidence/'noop.log').read_text()
    final = entries(a.evidence/'ninja-noop.log')
    executable_names = {'vcmitest','vcmiclient','vcmiserver','vcmilauncher','vcmieditor','vcmilobby'}
    noop_binary_edges = sorted(name for name, row in final.items() if after.get(name) != row and (pathlib.Path(name).name in executable_names or re.search(r'\.(?:o|obj|a|lib|dylib|exe|dll|so(?:\.[0-9.]+)?)$', name)))
    # CMake/Ninja's progress descriptions are stable and compiler/linker actions
    # are mandatory descriptions in the generated rules. Include C/C++ and Qt RC.
    forbidden = [line for line in noop.splitlines() if re.search(r'\[\d+/\d+\].*(?:Building .* object|Linking |Archiving )', line)]
    report = {'object_edges': len(objects), 'objects_by_target': dict(sorted(groups.items())), 'object_outputs': objects, 'all_executed_outputs': sorted(changed), 'noop_compile_or_link_edges': forbidden, 'noop_binary_outputs': noop_binary_edges, 'configure_seconds': int((a.evidence/'configure-seconds.txt').read_text()), 'build_seconds': int((a.evidence/'build-seconds.txt').read_text()), 'note': 'Object edges include ccache hits; actual compiler misses/hits are in ccache-after.tsv. No zero-miss promise for changed source.'}
    (a.evidence/'incremental-report.json').write_text(json.dumps(report, indent=2)+'\n')
    if forbidden or noop_binary_edges: raise SystemExit('FAIL: immediate repeat rebuilt compiler/linker edges')
    print(json.dumps(report, indent=2))
    import os
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
            stream.write('save-cache=' + ('true' if objects or any(pathlib.Path(name).name in executable_names or re.search(r'\.(?:a|lib|dylib|exe|dll|so(?:\.[0-9.]+)?)$', name) for name in changed) else 'false') + '\n')
if __name__ == '__main__': main()
