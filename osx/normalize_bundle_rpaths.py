#!/usr/bin/env python3
"""Remove non-system absolute LC_RPATH entries from a staged app before signing."""

import argparse
import platform
import posixpath
import re
import subprocess
from pathlib import Path


MAGICS = {bytes.fromhex(x) for x in ("feedface", "cefaedfe", "feedfacf", "cffaedfe",
                                    "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}
DYLIB_COMMANDS = {"LC_ID_DYLIB", "LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB",
                  "LC_REEXPORT_DYLIB", "LC_LAZY_LOAD_DYLIB", "LC_LOAD_UPWARD_DYLIB"}


def run(*args):
    return subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE).stdout


def parse_load_commands(output):
    dependencies, rpaths = [], []
    command = None
    pending = False
    for line in output.splitlines():
        line = line.strip()
        if line.startswith("cmd "):
            if pending:
                raise ValueError("Malformed LC_RPATH command")
            command = line[4:]
            pending = command == "LC_RPATH"
        elif command == "LC_RPATH" and line.startswith("path "):
            match = re.fullmatch(r"path (.+) \(offset [0-9]+\)", line)
            if not match:
                raise ValueError("Malformed LC_RPATH path")
            rpaths.append(match[1])
            pending = False
        elif command in DYLIB_COMMANDS and line.startswith("name "):
            dependencies.append((command, line[5:]))
    if pending:
        raise ValueError("Malformed LC_RPATH command")
    return dependencies, rpaths


def remove_rpath(value):
    if not value.startswith("/"):
        return False
    normalized = posixpath.normpath(value)
    return not any(normalized == root or normalized.startswith(root + "/")
                   for root in ("/usr/lib", "/System/Library"))


def normalize(app):
    if app.is_symlink():
        raise ValueError("The staged app must not be a symlink")
    app = app.resolve(strict=True)
    if app.suffix != ".app" or not (app / "Contents/MacOS").is_dir():
        raise ValueError("Expected a staged .app with Contents/MacOS")
    plans, seen = [], set()
    for path in sorted(app.rglob("*")):
        resolved = path.resolve()
        if not resolved.is_relative_to(app) or not path.exists():
            raise ValueError(f"External or broken bundle symlink: {path}")
        if not path.is_file() or resolved in seen:
            continue
        seen.add(resolved)
        with resolved.open("rb") as stream:
            if stream.read(4) not in MAGICS:
                continue
        if resolved.stat().st_nlink != 1:
            raise ValueError(f"Refusing to modify a hard-linked Mach-O image: {path}")
        dependencies, rpaths = parse_load_commands(run("/usr/bin/otool", "-arch", "all", "-l", str(resolved)))
        removed = list(dict.fromkeys(value for value in rpaths if remove_rpath(value)))
        if removed:
            archs = run("/usr/bin/lipo", "-archs", str(resolved)).split()
            if archs != ["arm64"]:
                raise ValueError(f"LC_RPATH cleanup requires a single arm64 slice: {path} ({archs})")
            plans.append((resolved, dependencies, rpaths, removed))
    for path, dependencies, rpaths, removed in plans:
        for value in removed:
            run("/usr/bin/install_name_tool", "-delete_rpath", value, str(path))
        after_dependencies, after_rpaths = parse_load_commands(run("/usr/bin/otool", "-arch", "all", "-l", str(path)))
        if after_dependencies != dependencies or after_rpaths != [value for value in rpaths if value not in removed]:
            raise ValueError(f"Unexpected load commands after LC_RPATH cleanup: {path}")
        print(f"Removed {len(removed)} non-system absolute rpath(s): {path.relative_to(app)}")
    return len(plans)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != "Darwin":
        parser.error("A native macOS host with lipo, otool and install_name_tool is required")
    normalize(args.app)


if __name__ == "__main__":
    main()
