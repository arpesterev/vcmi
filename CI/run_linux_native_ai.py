#!/usr/bin/env python3
"""Run an already-built Linux x86_64 vcmitest with isolated, pinned CI fixtures.

This is build-tree evidence, not packaged gameplay or neural inference evidence.
The caller supplies an existing archive; this script never downloads anything.
"""

import argparse
import json
import os
import platform
import signal
import stat
import subprocess
import tempfile
import time
from pathlib import Path


from run_macos_native_ai import (
    FIXTURE_SHA256, FIXTURE_BYTES, FIXTURE_FILES, FIXTURE_UNPACKED_BYTES,
    FIXTURE_ROOTS, GATED_NATIVE, FULL_NATIVE_FILTER, NATIVE_FILTER,
    sha256_file, copy_verified_archive, check_archive_listing, discover_archiver,
    matches_filter, parse_test_inventory, verify_native_inventory, validate_report,
    disable_core_dumps, selected_suites)
from check_native_ai_inventory import workflow_counts

SOURCE_COMMIT = "aafa3de7cd5d93dbec49dda1702ebc5c699e0827"
SOURCE_MANIFEST_SHA256 = "91e982139db20cd44e197bf1974676a46643b4b908e2c5fd414e903c764ce0b0"


def capture(command, *, env=None, timeout=60, cwd=None):
    with subprocess.Popen(command, env=env, cwd=cwd, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True, encoding="utf-8",
                          start_new_session=True, preexec_fn=disable_core_dumps) as process:
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.communicate()
            raise
        if process.returncode:
            raise subprocess.CalledProcessError(process.returncode, command, stdout, stderr)
        return stdout


def isolated_environment(home, temporary):
    """Do not inherit loader overrides, model gates, or GoogleTest controls."""
    return {
        "HOME": str(home),
        "TMPDIR": str(temporary),
        "XDG_DATA_HOME": str(home / ".local/share"),
        "XDG_CONFIG_HOME": str(home / ".config"),
        "XDG_CACHE_HOME": str(home / ".cache"),
        "XDG_STATE_HOME": str(home / ".local/state"),
        "XDG_DATA_DIRS": str(home / "system-data"),
        "XDG_CONFIG_DIRS": str(home / "system-config"),
        "PATH": "/usr/bin:/bin:/usr/sbin:/sbin",
        "LANG": "C",
        "LC_ALL": "C",
    }


def prepare_fixture(source, temporary, env, sevenzip):
    archive = temporary / "heroes3.7z"
    copy_verified_archive(source, archive)
    listing = capture([sevenzip, "l", "-slt", "-ba", str(archive)], env=env)
    entries = check_archive_listing(listing)
    data = Path(env["XDG_DATA_HOME"]) / "vcmi"
    data.mkdir(parents=True)
    capture([sevenzip, "x", "-y", "-o" + str(data), str(archive)], env=env)
    extracted = {}
    for path in data.rglob("*"):
        info = path.lstat()
        if stat.S_ISDIR(info.st_mode):
            value = (True, 0)
        elif stat.S_ISREG(info.st_mode) and info.st_nlink == 1:
            value = (False, info.st_size)
        else:
            raise ValueError("Unexpected extracted fixture file type")
        extracted[path.relative_to(data).as_posix()] = value
    if extracted != entries:
        raise ValueError("Extracted fixture differs from its verified listing")
    config = Path(env["XDG_CONFIG_HOME"]) / "vcmi"
    config.mkdir(parents=True)
    (config / "testModSettings.json").write_text(json.dumps({
        "activePreset": "default",
        "presets": {"default": {"mods": ["vcmi", "vcmi-test"]}},
    }) + "\n", encoding="utf-8")


def check_binary(binary, env):
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        raise ValueError("Native Linux x86_64 is required; no macOS qualification is claimed")
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise ValueError("vcmitest is missing or not executable")
    with binary.open("rb") as stream:
        header = stream.read(64)
    if (len(header) != 64 or header[:7] != b"\x7fELF\x02\x01\x01"
            or int.from_bytes(header[16:18], "little") not in (2, 3)
            or int.from_bytes(header[18:20], "little") != 62
            or int.from_bytes(header[20:24], "little") != 1):
        raise ValueError("vcmitest must be a 64-bit little-endian x86_64 ELF executable")
    for resource in ("config/filesystem.json", "Mods/vcmi/mod.json",
                     "scripts/damage/damageCalculator.lua", "test/testdata/vcmi-test/mod.json"):
        if not (binary.parent / resource).is_file():
            raise ValueError("Missing build-tree resource: " + resource)


def verify_source(source):
    manifest_path = Path(__file__).with_name("linux_native_source_manifest.json")
    if sha256_file(manifest_path) != SOURCE_MANIFEST_SHA256:
        raise ValueError("Source manifest does not match the frozen candidate")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for name, digest in manifest["files"].items():
        path = source / name
        if any(part.is_symlink() for part in [path, *path.parents]):
            raise ValueError("Symlink in candidate source: " + name)
        if not path.is_file() or sha256_file(path) != digest:
            raise ValueError("Source file differs from frozen candidate: " + name)
    allowed = {"CI/run_linux_native_ai.py", "CI/test_linux_native_ai.py",
               "CI/linux_native_source_manifest.json", "CI/reseal_linux_native_source.py"}
    for path in source.rglob("*"):
        name = path.relative_to(source).as_posix()
        if path.is_symlink():
            raise ValueError("Symlink in candidate source: " + name)
        if (path.is_file() and name not in manifest["files"] and name not in allowed
                and not (path.parent == source / "CI/__pycache__" and path.suffix == ".pyc")):
            raise ValueError("Unexpected candidate source file: " + name)
    return {"base_commit": manifest["base_commit"],
            "ai_overlay_patch_sha256": manifest["ai_overlay_patch_sha256"],
            "overlay_digest_scope": manifest["overlay_digest_scope"],
            "manifest_sha256": SOURCE_MANIFEST_SHA256, "verified_files": len(manifest["files"])}


def check_build_cache(binary, source):
    cache = binary.parent.parent / "CMakeCache.txt"
    values = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith(("#", "//")) and ":" in line and "=" in line:
            key, value = line.split("=", 1)
            key = key.split(":", 1)[0]
            if key in values:
                raise ValueError("Duplicate CMake cache key: " + key)
            values[key] = value
    if Path(values.get("CMAKE_HOME_DIRECTORY", "/missing")).resolve() != source:
        raise ValueError("Build cache does not identify the verified source directory")
    if (binary.name != "vcmitest" or binary.parent.name != "bin"
            or Path(values.get("CMAKE_CACHEFILE_DIR", "/missing")).resolve() != binary.parent.parent):
        raise ValueError("Binary is not in the identified build tree")
    enabled = ("SERVER", "TEST", "BATTLE_AI", "STUPID_AI", "NULLKILLER2_AI", "MMAI")
    disabled = ("CLIENT", "LAUNCHER", "EDITOR", "TRANSLATIONS", "VIDEO", "DISCORD",
                "LOBBY", "MINIMAL_LIB", "MMAI_TEST", "CCACHE", "LTO", "GITVERSION")
    for key, expected in [("ENABLE_" + k, "ON") for k in enabled] + [
            ("ENABLE_" + k, "OFF") for k in disabled] + [("COPY_CONFIG_ON_BUILD", "ON"),
            ("INSTALL_GTEST", "OFF"), ("CMAKE_BUILD_TYPE", "Release")]:
        if values.get(key) != expected:
            raise ValueError("Required CMake cache setting: " + key + "=" + expected)
    return {"path": str(cache), "sha256": sha256_file(cache),
            "provenance_limit": "Cache linkage and source hashes do not prove historical compiler inputs"}


def read_inventory(binary, output, env, name, selected):
    listing = capture([str(binary),
                       "--gtest_list_tests", "--gtest_filter=" + selected], env=env, cwd=binary.parent)
    (output / (name + "-inventory.log")).write_text(listing, encoding="utf-8")
    identities = parse_test_inventory(listing)
    if any(not matches_filter(identity, selected) for identity in identities):
        raise ValueError("Inventory contains a test outside the selected filter")
    return identities


def run_suite(binary, output, env, suite, inventory):
    name, selected, expected, timeout = suite
    xml = output / (name + ".xml")
    log = output / (name + ".log")
    result = {"name": name, "filter": selected, "expected_cases": expected,
              "timeout_seconds": timeout, "status": "failed", "errors": [],
              "xml": xml.name, "log": log.name}
    command = [str(binary), "--gtest_filter=" + selected,
               "--gtest_output=xml:" + str(xml)]
    started = time.monotonic()
    with log.open("x", encoding="utf-8") as stream:
        try:
            with subprocess.Popen(command, cwd=binary.parent, env=env, stdout=stream,
                                  stderr=subprocess.STDOUT, start_new_session=True,
                                  preexec_fn=disable_core_dumps) as process:
                try:
                    result["exit_code"] = process.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    process.wait()
                    result["errors"].append(f"Process timed out after {timeout} seconds")
                if process.returncode != 0:
                    result["errors"].append(f"Process exited with status {process.returncode}")
        except OSError as error:
            result["errors"].append("Could not execute vcmitest: " + str(error))
    result["elapsed_seconds"] = round(time.monotonic() - started, 3)
    try:
        result["xml_completed_cases"] = validate_report(xml, inventory, selected)
    except (ValueError, TypeError) as error:
        result["errors"].append(str(error))
    if not result["errors"]:
        result["status"] = "passed"
        result["passed_cases"] = expected
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-binary", type=Path, required=True)
    parser.add_argument("--fixture-archive", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True,
                        help="New or empty directory for JSON, logs and XML only")
    args = parser.parse_args(argv)
    output = args.output_dir
    if output.is_symlink() or (output.exists() and (not output.is_dir() or any(output.iterdir()))):
        parser.error("Output directory must be new or empty and must not be a symlink")
    output.mkdir(parents=True, exist_ok=True)
    output = output.resolve()
    report = {"status": "failed", "evidence": "build-tree-native-tests",
              "platform": "Linux-x86_64", "source_sha": SOURCE_COMMIT,
              "fixture_sha256": FIXTURE_SHA256, "isolated_home": True,
              "scope": "Non-neural native regressions; no packaged gameplay or neural inference claim",
              "excluded_gated_tests": sorted(GATED_NATIVE),
              "suites": []}
    try:
        source = args.source_dir.resolve(strict=True)
        report["source_checks"] = verify_source(source)
        counts = workflow_counts((source / ".github/workflows/ai-macos-arm64.yml").read_text())
        report["expected_counts"] = counts
        binary = args.test_binary.resolve(strict=True)
        report["build_cache"] = check_build_cache(binary, source)
        fixture = args.fixture_archive.resolve(strict=True)
        with tempfile.TemporaryDirectory(prefix="vcmi-linux-native-", dir=output) as temporary_name:
            temporary = Path(temporary_name)
            home, scratch = temporary / "home", temporary / "tmp"
            home.mkdir()
            scratch.mkdir()
            sevenzip = discover_archiver()
            env = isolated_environment(home, scratch)
            check_binary(binary, env)
            report["binary_checks"] = {"architecture": "x86_64", "format": "ELF64"}
            report["binary_sha256"] = sha256_file(binary)
            full_inventory = read_inventory(binary, output, env, "full-native", FULL_NATIVE_FILTER)
            native_inventory = verify_native_inventory(
                full_inventory, counts["native"], counts["forecast"], counts["lifecycle"])
            suites = selected_suites(counts["native"], counts["forecast"])
            inventories = {}
            for suite in suites:
                name, selected, expected, _ = suite
                inventories[name] = read_inventory(binary, output, env, name, selected)
                if len(inventories[name]) != expected:
                    raise ValueError(f"{name}: selected inventory count differs from expected {expected}")
                if name == "native-port-battles" and inventories[name] != native_inventory:
                    raise ValueError("Selected native inventory differs from full inventory minus exact gates")
            prepare_fixture(fixture, temporary, env, sevenzip)
            for suite in suites:
                report["suites"].append(run_suite(binary, output, env, suite, inventories[suite[0]]))
            if sha256_file(binary) != report["binary_sha256"]:
                raise ValueError("vcmitest bytes changed during the test run")
            verify_source(source)
            if check_build_cache(binary, source) != report["build_cache"]:
                raise ValueError("Build cache changed during the test run")
            if all(suite["status"] == "passed" for suite in report["suites"]):
                report["status"] = "passed"
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
        if isinstance(error, subprocess.CalledProcessError):
            report["diagnostic"] = (error.stderr or "")[-4000:]
    (output / "native-ai-summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
