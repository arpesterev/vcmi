#!/usr/bin/env python3
"""Execute the packaged ARM64 server's data-free CLI in a fresh temporary HOME."""

import argparse
import json
import platform
import plistlib
import re
import subprocess
import tempfile
from pathlib import Path


def run_cli(server, option, home):
    result = subprocess.run(["/usr/bin/arch", "-arm64", str(server), option],
                            cwd=server.parent,
                            env={"HOME": str(home), "TMPDIR": str(home / "tmp"),
                                 "PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C"},
                            stdin=subprocess.DEVNULL, capture_output=True, text=True,
                            timeout=30, check=False)
    if result.returncode != 0:
        raise ValueError(f"Packaged server {option} exited {result.returncode}:\n"
                         f"{result.stdout}\n{result.stderr}")
    return result


def smoke(app, expected_sha):
    if not re.fullmatch(r"[0-9a-f]{40}", expected_sha):
        raise ValueError("Expected commit must be a complete lowercase Git SHA")
    app = app.resolve(strict=True)
    with (app / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    if info.get("CFBundleVersion") != expected_sha:
        raise ValueError("Bundle version does not match the exact source commit")
    server = app / "Contents/MacOS/vcmiserver"
    if not server.is_file() or not server.resolve().is_relative_to(app):
        raise ValueError("Missing or external packaged server")
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)],
                   check=True, timeout=30)
    with tempfile.TemporaryDirectory(prefix="vcmi-packaged-smoke-") as directory:
        home = Path(directory).resolve()
        (home / "tmp").mkdir()
        help_result = run_cli(server, "--help", home)
        if not all(token in help_result.stdout for token in ("Allowed options", "--help", "--version")):
            raise ValueError("Packaged server did not return its expected help")
        version_result = run_cli(server, "--version", home)
        match = re.search(rf"(?m)^([0-9]+\.[0-9]+\.[0-9]+\.{expected_sha})\r?$", version_result.stdout)
        if not match:
            raise ValueError("Packaged server did not report the exact source commit")
        data = home / "Library/Application Support/vcmi"
        expected_paths = {"user data": data, "user cache": data,
                          "user config": data / "config", "user logs": home / "Library/Logs/vcmi",
                          "user saves": data / "Saves", "user extracted": data / "extracted"}
        for label, expected in expected_paths.items():
            reported = re.search(rf"(?m)^\s*{label}:\s+([^\r\n]+)", version_result.stdout)
            if not reported or Path(reported[1]).resolve() != expected:
                raise ValueError(f"Packaged server {label} escaped the isolated HOME")
        commands = [{"arguments": ["/usr/bin/arch", "-arm64", "Contents/MacOS/vcmiserver", option],
                     "exit_code": result.returncode,
                     "stdout": result.stdout.replace(str(home), "<isolated-home>"),
                     "stderr": result.stderr.replace(str(home), "<isolated-home>")}
                    for option, result in (("--help", help_result), ("--version", version_result))]
    return {"schema_version": 1, "source_commit": expected_sha,
            "server_version": match[1], "architecture": "arm64 (forced by arch -arm64)",
            "commands": commands, "isolated_home_paths": "verified",
            "launcher_cli": "not run: launcher/main.cpp has no help/version CLI and opens its GUI",
            "scope": "Packaged server CLI startup only; no game-data loading, AI inference or launcher GUI test"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--expected-sha", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("A native ARM64 macOS host is required")
    result = smoke(args.app, args.expected_sha)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"Packaged ARM64 server help/version passed for {args.expected_sha}")


if __name__ == "__main__":
    main()
