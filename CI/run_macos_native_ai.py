#!/usr/bin/env python3
"""Run an already-built ARM64 vcmitest with isolated, pinned CI fixtures.

This is build-tree evidence, not packaged gameplay or neural inference evidence.
The caller supplies an existing archive; this script never downloads anything.
"""

import argparse
import fnmatch
import hashlib
import json
import os
import platform
import re
import shutil
import signal
import stat
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
from pathlib import Path, PurePosixPath


FIXTURE_SHA256 = "64247aeee0342c6f349576ba700a81e2de46de7b9aa19ca4881b8d59f5e0ced8"
FIXTURE_BYTES = 147594
FIXTURE_FILES = 10478
FIXTURE_UNPACKED_BYTES = 959915
FIXTURE_ROOTS = {"Data", "Sprites", "Sounds", "Mp3", "VIDEO"}
GATED_NATIVE_NAMES = (
    "mmaiV13StrictRejectsInvalidPack",
    "mmaiV13AttackerCompletesBattle",
    "mmaiV13DefenderCompletesBattle",
    "mmaiV13BothNeuralSidesCompleteBattle",
    "mmaiV13SiegeDefenderCompletesBattle",
    "mmaiV13StrictNeuralDoesNotCastHeroSpell",
    "mmaiOppositeSideMetadataIsRejected",
    "mmaiFirstAidWithoutWoundsUsesEngineAutoHealRealBattle",
    "mmaiWithRealNeuralModelCompletesRealBattle",
    "mmaiStrictNeuralDoesNotCastScriptedHeroSpellRealBattle",
    "mmaiDefenderNeuralModelCompletesRealBattle",
    "mmaiSiegeDefenderNeuralModelCompletesRealBattle",
    "mmaiRangedAttackerAgainstFHeroes2InfantryRealBattle",
    "mmaiDefenderAgainstFHeroes2RangedAttackerRealBattle",
    "mmaiBothNeuralSidesFightRealBattle",
    "mmaiSiegeAttackerAgainstTUMDefenderRealBattle",
    "mmaiAttackerAgainstHoMM3DefenderRealBattle",
    "mmaiDefenderAgainstHoMM3AttackerRealBattle",
    "mmaiAttackerAgainstTUMDefenderRealBattle",
    "mmaiDefenderAgainstTUMAttackerRealBattle",
)
GATED_NATIVE = {"AIIntegrationNativeBattle." + name for name in GATED_NATIVE_NAMES}
FULL_NATIVE_FILTER = "AIIntegrationNativeBattle.*:HoMM3ForecastQueueNative.*:HoMM3ForecastLifecycleNative.*"
NATIVE_FILTER = FULL_NATIVE_FILTER + "-" + ":".join(sorted(GATED_NATIVE))


def selected_suites(native_count, forecast_count):
    return (
        ("native-port-battles", NATIVE_FILTER, native_count + forecast_count, 300),
        ("mmai-onnx-output-contracts", "MMAINNOutputValidation.*", 48, 90),
        ("nullkiller-regressions", "Nullkiller2_*:ResourceTradingTurnTest.*", 145, 900),
    )


def capture(command, *, env=None, timeout=60, cwd=None):
    result = subprocess.run(command, env=env, cwd=cwd, check=True, timeout=timeout,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, encoding="utf-8")
    return result.stdout


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1048576):
            digest.update(chunk)
    return digest.hexdigest()


def isolated_environment(home, temporary):
    """Do not inherit dyld overrides, model gates, or GoogleTest controls."""
    return {
        "HOME": str(home),
        "TMPDIR": str(temporary),
        "PATH": "/usr/bin:/bin:/usr/sbin:/sbin",
        "LANG": "C",
        "LC_ALL": "C",
    }


def copy_verified_archive(source, destination):
    digest = hashlib.sha256()
    size = 0
    with source.open("rb") as incoming, destination.open("xb") as outgoing:
        while chunk := incoming.read(65536):
            size += len(chunk)
            if size > FIXTURE_BYTES:
                raise ValueError("Fixture archive has an unexpected size")
            digest.update(chunk)
            outgoing.write(chunk)
    if size != FIXTURE_BYTES or digest.hexdigest() != FIXTURE_SHA256:
        raise ValueError("Fixture archive does not match the pinned size and SHA-256")


def check_archive_listing(listing):
    entries = {}
    total_bytes = 0
    directories = set()
    for block in re.split(r"\n\s*\n", listing.strip()):
        fields = {}
        for line in block.splitlines():
            if " = " in line:
                key, value = line.split(" = ", 1)
                if key in fields:
                    raise ValueError("Duplicate archive listing field")
                fields[key] = value
        if not fields:
            continue
        if "Path" not in fields or "Size" not in fields:
            raise ValueError("Malformed archive listing")
        raw = fields["Path"]
        path = PurePosixPath(raw)
        if (not raw or path.is_absolute() or "\\" in raw or ":" in raw
                or any(ord(char) < 32 for char in raw)
                or any(part in ("", ".", "..") for part in raw.split("/"))
                or path.parts[0] not in FIXTURE_ROOTS or raw in entries):
            raise ValueError("Unsafe or duplicate archive path: " + raw)
        if any("link" in key.lower() and value for key, value in fields.items()):
            raise ValueError("Links are not allowed in the fixture archive")
        attributes = fields.get("Attributes", "")
        if any(len(token) == 10 and token[0] not in ("-", "d")
               for token in attributes.split()):
            raise ValueError("Unexpected fixture archive file type")
        if fields.get("Encrypted", "-") != "-":
            raise ValueError("Encrypted fixture archive entry")
        is_directory = attributes.startswith("D") or fields.get("Folder") == "+"
        size = int(fields["Size"])
        if size < 0 or (is_directory and size != 0):
            raise ValueError("Invalid fixture archive entry size")
        if is_directory:
            directories.add(raw)
        elif len(path.parts) != 2:
            raise ValueError("Unexpected nested fixture file")
        entries[raw] = (is_directory, size)
        total_bytes += size
    if (directories != FIXTURE_ROOTS
            or len(entries) != FIXTURE_FILES + len(FIXTURE_ROOTS)
            or total_bytes != FIXTURE_UNPACKED_BYTES):
        raise ValueError("Fixture listing does not match the pinned layout")
    return entries


def discover_archiver():
    sevenzip = next((executable for name in ("7zz", "7z", "7za")
                     if (executable := shutil.which(name))), None)
    if sevenzip is None:
        raise ValueError("No installed 7zz, 7z, or 7za executable")
    return str(Path(sevenzip).resolve(strict=True))


def prepare_fixture(source, temporary, env, sevenzip):
    archive = temporary / "heroes3.7z"
    copy_verified_archive(source, archive)
    listing = capture([sevenzip, "l", "-slt", "-ba", str(archive)], env=env)
    entries = check_archive_listing(listing)
    data = Path(env["HOME"]) / "Library/Application Support/vcmi"
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
    config = data / "config"
    config.mkdir()
    (config / "testModSettings.json").write_text(json.dumps({
        "activePreset": "default",
        "presets": {"default": {"mods": ["vcmi", "vcmi-test"]}},
    }) + "\n", encoding="utf-8")


def check_binary(binary, env):
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise ValueError("Native macOS ARM64 is required; no emulated result is accepted")
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise ValueError("vcmitest is missing or not executable")
    required = ("config/filesystem.json", "Mods/vcmi/mod.json",
                "scripts/damage/damageCalculator.lua", "test/testdata/vcmi-test/mod.json")
    for resource in required:
        if not (binary.parent / resource).is_file():
            raise ValueError("Missing build-tree resource: " + resource)
    archs = capture(["/usr/bin/lipo", "-archs", str(binary)], env=env).split()
    if archs != ["arm64"]:
        raise ValueError("vcmitest must have exactly one arm64 slice")
    capture(["/usr/bin/codesign", "--verify", "--strict", "--verbose=4", str(binary)], env=env)


def matches_filter(identity, selected):
    included, _, excluded = selected.partition("-")
    return (any(fnmatch.fnmatchcase(identity, part) for part in included.split(":"))
            and not any(fnmatch.fnmatchcase(identity, part) for part in excluded.split(":") if part))


def parse_test_inventory(listing):
    suite = None
    identities = set()
    for line in listing.splitlines():
        if not line.strip() or line.startswith("Note: Google Test filter = "):
            continue
        name = line.split("#", 1)[0].strip()
        if not line[0].isspace() and name.endswith("."):
            suite = name[:-1]
            if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_/]*", suite):
                raise ValueError("Malformed GoogleTest suite name")
        elif line[0].isspace() and suite and re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_/]*", name):
            identity = suite + "." + name
            if identity in identities:
                raise ValueError("Duplicate test in GoogleTest inventory")
            identities.add(identity)
        else:
            raise ValueError("Malformed GoogleTest inventory line: " + line)
    if not identities:
        raise ValueError("GoogleTest inventory is empty")
    return identities


def read_inventory(binary, output, env, name, selected):
    listing = capture(["/usr/bin/arch", "-arm64", str(binary),
                       "--gtest_list_tests", "--gtest_filter=" + selected], env=env, cwd=binary.parent)
    (output / (name + "-inventory.log")).write_text(listing, encoding="utf-8")
    identities = parse_test_inventory(listing)
    if any(not matches_filter(identity, selected) for identity in identities):
        raise ValueError("Inventory contains a test outside the selected filter")
    return identities


def verify_native_inventory(identities, native_count, forecast_count, lifecycle_count=0):
    native = {identity for identity in identities if identity.startswith("AIIntegrationNativeBattle.")}
    queue = {identity for identity in identities if identity.startswith("HoMM3ForecastQueueNative.")}
    lifecycle = {identity for identity in identities if identity.startswith("HoMM3ForecastLifecycleNative.")}
    forecast = queue | lifecycle
    possible_gates = {identity for identity in native
                      if any(fnmatch.fnmatchcase(identity, pattern) for pattern in (
                          "AIIntegrationNativeBattle.mmaiV13*",
                          "AIIntegrationNativeBattle.mmai*RealBattle",
                          "AIIntegrationNativeBattle.mmaiOppositeSideMetadataIsRejected"))}
    if possible_gates != GATED_NATIVE:
        raise ValueError("Native inventory does not contain exactly the reviewed 20 gated names")
    if (len(native) != native_count + len(GATED_NATIVE) or len(forecast) != forecast_count
            or len(lifecycle) != lifecycle_count or native | forecast != identities):
        raise ValueError("Native/forecast inventory differs from the caller's expected candidate counts")
    return identities - GATED_NATIVE


def validate_report(path, inventory, selected):
    expected = len(inventory)
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError) as error:
        raise ValueError("Missing or malformed GoogleTest XML: " + str(error)) from error
    if root.tag not in ("testsuites", "testsuite"):
        raise ValueError("Unexpected GoogleTest XML root")
    if int(root.get("tests", "-1")) != expected:
        raise ValueError(f"Expected {expected} tests in XML totals")
    for suite in (element for element in root.iter() if element.tag in ("testsuites", "testsuite")):
        if any(int(suite.get(name, "0")) != 0 for name in ("failures", "errors", "disabled", "skipped")):
            raise ValueError("XML totals contain failures, errors, disabled or skipped tests")
    if any(element.tag in ("failure", "error", "skipped") for element in root.iter()):
        raise ValueError("XML contains a failure, error or skipped test")
    cases = list(root.iter("testcase"))
    identities = [(case.get("classname", ""), case.get("name", "")) for case in cases]
    if len(cases) != expected or len(set(identities)) != expected:
        raise ValueError(f"Expected {expected} distinct executed test cases")
    for case, (classname, name) in zip(cases, identities):
        if (not classname or not name or not matches_filter(classname + "." + name, selected)
                or case.get("status") != "run" or case.get("result") != "completed"):
            raise ValueError("Unselected or incomplete test case in XML")
    if {classname + "." + name for classname, name in identities} != inventory:
        raise ValueError("XML identities differ from the selected GoogleTest inventory")
    return len(cases)


def disable_core_dumps():
    import resource
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run_suite(binary, output, env, suite, inventory):
    name, selected, expected, timeout = suite
    xml = output / (name + ".xml")
    log = output / (name + ".log")
    result = {"name": name, "filter": selected, "expected_cases": expected,
              "timeout_seconds": timeout, "status": "failed", "errors": [],
              "xml": xml.name, "log": log.name}
    command = ["/usr/bin/arch", "-arm64", str(binary), "--gtest_filter=" + selected,
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
    parser.add_argument("--source-sha", required=True,
                        help="Exact checkout SHA supplied by the calling workflow")
    parser.add_argument("--expected-native-count", required=True, type=int,
                        help="Reviewed candidate's non-neural AIIntegrationNativeBattle count")
    parser.add_argument("--expected-forecast-count", required=True, type=int,
                        help="Reviewed candidate's combined queue and lifecycle forecast count")
    parser.add_argument("--expected-lifecycle-count", type=int, default=0,
                        help="Reviewed HoMM3ForecastLifecycleNative subset of the forecast count")
    parser.add_argument("--output-dir", type=Path, required=True,
                        help="New or empty directory for JSON, logs and XML only")
    args = parser.parse_args(argv)
    output = args.output_dir
    if output.is_symlink() or (output.exists() and (not output.is_dir() or any(output.iterdir()))):
        parser.error("Output directory must be new or empty and must not be a symlink")
    output.mkdir(parents=True, exist_ok=True)
    output = output.resolve()
    report = {"status": "failed", "evidence": "build-tree-native-tests",
              "source_sha": args.source_sha, "source_sha_origin": "caller argument",
              "fixture_sha256": FIXTURE_SHA256, "isolated_home": True,
              "scope": "Non-neural native regressions; no packaged gameplay or neural inference claim",
              "expected_native_count": args.expected_native_count,
              "expected_forecast_count": args.expected_forecast_count,
              "expected_lifecycle_count": args.expected_lifecycle_count,
              "excluded_gated_tests": sorted(GATED_NATIVE),
              "suites": []}
    try:
        if not re.fullmatch(r"[0-9a-f]{40}", args.source_sha):
            raise ValueError("Source SHA must be a full lowercase 40-hex commit ID")
        if (args.expected_native_count <= 0 or args.expected_forecast_count < 0
                or not 0 <= args.expected_lifecycle_count <= args.expected_forecast_count):
            raise ValueError("Expected counts require positive native, nonnegative forecast and lifecycle within forecast")
        binary = args.test_binary.resolve(strict=True)
        fixture = args.fixture_archive.resolve(strict=True)
        with tempfile.TemporaryDirectory(prefix="vcmi-macos-native-") as temporary_name:
            temporary = Path(temporary_name)
            home, scratch = temporary / "home", temporary / "tmp"
            home.mkdir()
            scratch.mkdir()
            sevenzip = discover_archiver()
            env = isolated_environment(home, scratch)
            check_binary(binary, env)
            report["binary_checks"] = {"architecture": "arm64", "codesign_verify_strict": "passed"}
            report["binary_sha256"] = sha256_file(binary)
            (output / "binary-dependencies.log").write_text(
                capture(["/usr/bin/otool", "-L", str(binary)], env=env), encoding="utf-8")
            full_inventory = read_inventory(binary, output, env, "full-native", FULL_NATIVE_FILTER)
            native_inventory = verify_native_inventory(
                full_inventory, args.expected_native_count, args.expected_forecast_count, args.expected_lifecycle_count)
            suites = selected_suites(args.expected_native_count, args.expected_forecast_count)
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
