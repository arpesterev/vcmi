#!/usr/bin/env python3
"""Read-only architecture, signature and dependency audit of a VCMI app."""

import argparse
import hashlib
import json
import platform
import plistlib
import re
import subprocess
from pathlib import Path


BACKENDS = ("HoMM3OriginalTactics", "FHeroes2Tactics", "TUMEnchantedTactics", "MMAI")
ENABLED = ("ENABLE_CLIENT", "ENABLE_SERVER", "ENABLE_LAUNCHER", "ENABLE_BATTLE_AI",
           "ENABLE_STUPID_AI", "ENABLE_MMAI", "ENABLE_NULLKILLER2_AI")
MAGICS = {bytes.fromhex(x) for x in ("feedface", "cefaedfe", "feedfacf", "cffaedfe",
                                    "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}
SYSTEM_ROOTS = ("/usr/lib/", "/System/Library/")


def run(*args):
    return subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE).stdout


def read_cache(path):
    result = {}
    for line in path.read_text().splitlines():
        match = re.match(r"([^#/:][^:]*):[^=]+=(.*)$", line)
        if match:
            result[match[1]] = match[2]
    for name in ENABLED:
        if result.get(name, "").upper() not in ("ON", "TRUE", "1"):
            raise ValueError(f"{name} must be ON, got {result.get(name)!r}")
    if result.get("CMAKE_OSX_ARCHITECTURES") != "arm64":
        raise ValueError("The package must be configured explicitly for arm64")
    return {key: result[key] for key in (*ENABLED, "CMAKE_OSX_ARCHITECTURES")}


def parse_load_commands(output):
    dependencies, rpaths = [], []
    command = None
    for line in output.splitlines():
        line = line.strip()
        if line.startswith("cmd "):
            command = line[4:]
        elif command == "LC_RPATH" and line.startswith("path "):
            rpaths.append(line[5:].rsplit(" (offset ", 1)[0])
        elif command in ("LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB",
                          "LC_LAZY_LOAD_DYLIB", "LC_LOAD_UPWARD_DYLIB") and line.startswith("name "):
            dependencies.append(line[5:].rsplit(" (offset ", 1)[0])
    return dependencies, rpaths


def inside(path, root):
    return path.resolve().is_relative_to(root.resolve())


def expand_path(value, loader, executable):
    for prefix, directory in (("@loader_path", loader.parent),
                              ("@executable_path", executable.parent)):
        if value == prefix:
            return directory.resolve()
        if value.startswith(prefix + "/"):
            return (directory / value[len(prefix) + 1:]).resolve()
    if value.startswith("/"):
        return Path(value).resolve()
    raise ValueError(f"Unsupported load path {value!r} in {loader.name}")


def resolve_dependency(value, loader, executable, rpaths, app):
    if value.startswith(SYSTEM_ROOTS):
        return None
    if value.startswith("@rpath/"):
        candidates = [root / value[len("@rpath/"):] for root in rpaths]
    else:
        candidates = [expand_path(value, loader, executable)]
    for candidate in candidates:
        if inside(candidate, app) and candidate.is_file():
            return candidate.resolve()
    raise ValueError(f"Unresolved or external dependency {value!r} in {loader.relative_to(app)}")


def audit(app, cache, expected_sha):
    if not re.fullmatch(r"[0-9a-f]{40}", expected_sha):
        raise ValueError("Expected commit must be a complete lowercase Git SHA")
    settings = read_cache(cache)
    app = app.resolve()
    with (app / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    if info.get("CFBundleVersion") != expected_sha:
        raise ValueError("Bundle version does not match the exact source commit")
    if info.get("CFBundleExecutable") != "vcmilauncher":
        raise ValueError("The application does not launch vcmilauncher")

    binaries = app / "Contents/MacOS"
    executables = [binaries / name for name in ("vcmilauncher", "vcmiclient", "vcmiserver")]
    images, records = {}, []
    for path in sorted(app.rglob("*")):
        if path.is_symlink() and (not inside(path, app) or not path.exists()):
            raise ValueError(f"External or broken bundle symlink: {path.relative_to(app)}")
        if not path.is_file() or path.resolve() in images:
            continue
        if path.suffix.lower() == ".onnx":
            raise ValueError("Neural weights must be installed separately through the mod manager")
        if path.name.lower() in ("h3bitmap.lod", "h3sprite.lod", "heroes3.snd", "video.vid"):
            raise ValueError("Heroes III game archives must not be included in this package")
        with path.open("rb") as stream:
            magic = stream.read(4)
        if magic not in MAGICS:
            continue
        path = path.resolve()
        archs = run("/usr/bin/lipo", "-archs", str(path)).strip().split()
        if "arm64" not in archs:
            raise ValueError(f"Missing arm64 slice: {path.relative_to(app)} ({archs})")
        dependencies, rpaths = parse_load_commands(run("/usr/bin/otool", "-arch", "arm64", "-l", str(path)))
        run("/usr/bin/codesign", "--verify", "--strict", str(path))
        images[path] = (dependencies, rpaths)
        records.append({"path": str(path.relative_to(app)), "architectures": archs,
                        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        "dependencies": dependencies, "rpaths": rpaths,
                        "signature_integrity": "verified"})
    for path in executables:
        if path.resolve() not in images:
            raise ValueError(f"Missing Mach-O executable: {path.name}")
    facades = [path for path in images if path.name.startswith("libvcmi") and path.suffix == ".dylib"]
    if not facades:
        raise ValueError("Missing shared libvcmi facade")
    facade_bytes = b"".join(path.read_bytes() for path in facades)
    for backend in BACKENDS:
        if backend.encode() not in facade_bytes:
            raise ValueError(f"Missing backend identity in libvcmi: {backend}")

    visited = set()

    def visit(path, executable, inherited):
        dependencies, raw_rpaths = images[path]
        own = [expand_path(value, path, executable) for value in raw_rpaths]
        for root in own:
            if not inside(root, app) and not str(root).startswith(SYSTEM_ROOTS):
                raise ValueError(f"External build-machine rpath in {path.relative_to(app)}: {root}")
        search = tuple(dict.fromkeys(own + list(inherited)))
        key = (path, executable, search)
        if key in visited:
            return
        visited.add(key)
        for value in dependencies:
            target = resolve_dependency(value, path, executable, search, app)
            if target is not None:
                if target not in images:
                    raise ValueError(f"Dependency is not a bundled Mach-O image: {value}")
                visit(target, executable, search)

    for executable in executables:
        visit(executable.resolve(), executable.resolve(), ())
    # Qt plugins load at runtime and are not necessarily in the launcher's load graph.
    launcher = executables[0].resolve()
    launcher_rpaths = [expand_path(value, launcher, launcher) for value in images[launcher][1]]
    for path in images:
        visit(path, launcher, launcher_rpaths)
    return {"schema_version": 1, "source_commit": expected_sha,
            "engine_version": info.get("CFBundleShortVersionString"), "build_options": settings,
            "backend_identity_strings": list(BACKENDS), "images": records,
            "scope": "Static bundle integrity only; not launcher persistence, neural inference or gameplay",
            "app_bundle_seal": "not checked; individual Mach-O signatures verified",
            "notarization": "not checked", "known_h3_archives_found": False,
            "onnx_files_found": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--build-cache", type=Path, required=True)
    parser.add_argument("--expected-sha", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != "Darwin":
        parser.error("A native macOS host with lipo, otool and codesign is required")
    result = audit(args.app, args.build_cache, args.expected_sha)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"Audited {len(result['images'])} arm64 Mach-O images for {args.expected_sha}")


if __name__ == "__main__":
    main()
