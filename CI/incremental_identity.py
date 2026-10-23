#!/usr/bin/env python3
"""Exact compatibility scope for persisted Ninja outputs; no cross-toolchain fallback."""
import argparse, hashlib, json, os, pathlib, platform, subprocess

def run(*args): return subprocess.check_output(args, text=True).strip()
def digest(path): return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def tree_digest(root):
    root = pathlib.Path(root).expanduser()
    if not root.is_dir(): raise RuntimeError('Required dependency tree missing: ' + str(root))
    entries = []
    for path in sorted(root.rglob('*')):
        rel = str(path.relative_to(root))
        if path.is_symlink(): entries.append((rel, 'symlink', os.readlink(path)))
        elif path.is_file(): entries.append((rel, 'file', digest(path)))
    if not entries: raise RuntimeError('Empty dependency tree: ' + str(root))
    return hashlib.sha256(json.dumps(entries, separators=(',', ':')).encode()).hexdigest()
def identity(profile, installed=False):
    root = pathlib.Path.cwd().resolve()
    mac = profile.startswith('macos')
    compiler = 'clang++' if mac else 'g++'
    result = {'schema': 1, 'profile': profile, 'workspace': str(root), 'home': str(pathlib.Path.home()), 'os': platform.system(), 'arch': platform.machine(), 'runner_image': os.environ.get('ImageVersion', 'unknown'), 'cmake': run('cmake','--version'), 'ninja': run('ninja','--version'), 'bootstrap_compiler': run(compiler,'--version'), 'dependency_commit': run('git','-C','dependencies','rev-parse','HEAD'), 'presets_sha256': digest('CMakePresets.json'), 'build_contract_sha256': digest('CI/incremental-build.sh'), 'cache_protocol_sha256': digest('CI/incremental_cache.py')}
    if result['runner_image'] == 'unknown': raise RuntimeError('ImageVersion missing: cannot certify a hosted runner identity')
    if mac:
        result['xcode'] = run('xcodebuild','-version')
        result['sdk'] = run('xcrun','--show-sdk-version')
        result['sdk_path'] = run('xcrun','--show-sdk-path')
        result['dependency_archive_sha256'] = '88c0a27d137ae27cc86da5762d9f50c9080be13380afd2b416de3335a4ac8dd0'
        result['conan_version'] = '2.33.0'
        result['conanfile_sha256'] = {str(p): digest(p) for p in list(root.glob('conanfile.*')) + list(root.glob('conan*.lock'))}
    else:
        result['dependency_setup_sha256'] = digest('CI/before_install/linux_qt6.sh')
        result['onnx_setup_sha256'] = digest('CI/before_install/linux_onnxruntime.sh')
    if installed:
        result['ccache'] = run('ccache','--version')
        result['ccache_sha256'] = digest(run('which','ccache'))
        if mac:
            result['compiler_sha256'] = digest(run('xcrun','--find','clang++'))
            result['conan_packages_sha256'] = tree_digest('~/.conan2/p')
            result['conan_generated_sha256'] = tree_digest('conan-generated')
        else:
            result['onnxruntime_sha256'] = tree_digest('/opt/onnxruntime')
        if not mac:
            result['compiler'] = run('g++-14','--version')
            result['compiler_sha256'] = digest(run('which','g++-14'))
            result['packages'] = run('dpkg-query','-W','-f=${binary:Package}=${Version}\n')
    return result

def main():
    p=argparse.ArgumentParser();p.add_argument('profile');p.add_argument('--installed', action='store_true');p.add_argument('--scope', choices=['build','dependencies'],default='build');p.add_argument('--output',required=True);a=p.parse_args()
    value=identity(a.profile,a.installed)
    if a.scope == 'dependencies':
        for key in ['build_contract_sha256','cache_protocol_sha256','presets_sha256']: value.pop(key,None)
    encoded=json.dumps(value,sort_keys=True,indent=2)+'\n';pathlib.Path(a.output).write_text(encoded)
    print(hashlib.sha256(encoded.encode()).hexdigest())
if __name__=='__main__':main()
