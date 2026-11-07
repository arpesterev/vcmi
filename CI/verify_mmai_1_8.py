#!/usr/bin/env python3
"""Verify official MMAI release bytes without extracting, installing or enabling it."""

import argparse
import hashlib
import json
import re
from pathlib import Path
from zipfile import ZipFile


PIN = json.loads(Path(__file__).with_name("mmai_1_8_release.json").read_text())


def version(value):
    if not re.fullmatch(r"\d+\.\d+\.\d+", value):
        raise ValueError("Engine version must be a numeric major.minor.patch")
    return tuple(int(part) for part in value.split("."))


def verify(read_file, engine_version):
    if not version(PIN["engine_min"]) <= version(engine_version) <= version(PIN["engine_max"]):
        raise ValueError(f"MMAI {PIN['mod_version']} does not support VCMI {engine_version}")
    files = {}
    for name, expected in PIN["files"].items():
        content = read_file(name)
        actual = hashlib.sha256(content).hexdigest()
        if actual != expected:
            raise ValueError(f"Official MMAI file differs or is damaged: {name}")
        files[name] = actual
    manifest = json.loads(read_file("mod.json"))
    settings = json.loads(read_file("config/mmai-settings.json"))
    if manifest.get("compatibility") != {"min": PIN["engine_min"], "max": PIN["engine_max"]}:
        raise ValueError("MMAI compatibility metadata must remain unchanged")
    if settings.get("models") != PIN["models"]:
        raise ValueError("Official attacker/defender/siege model assignments must remain unchanged")
    return {"release_url": PIN["release_url"], "release_id": PIN["release_id"],
            "asset_id": PIN["asset_id"], "source_commit": PIN["source_commit"],
            "mod_version": PIN["mod_version"], "engine_version": engine_version,
            "model_version": PIN["model_version"], "models": PIN["models"], "files": files,
            "scope": "Byte identity only; enabling the mod and native inference remain separate checks"}


def verify_archive(path, engine_version):
    if path.stat().st_size != PIN["archive_size"]:
        raise ValueError("Unexpected archive size; do not replace the pin without revalidating the release")
    if hashlib.sha256(path.read_bytes()).hexdigest() != PIN["archive_sha256"]:
        raise ValueError("Archive SHA-256 mismatch; do not install this file")
    with ZipFile(path) as archive:
        return verify(lambda name: archive.read("mmai/" + name), engine_version)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--archive", type=Path)
    source.add_argument("--installed-mod", type=Path)
    parser.add_argument("--engine-version", required=True)
    args = parser.parse_args()
    if args.archive:
        result = verify_archive(args.archive, args.engine_version)
    else:
        result = verify(lambda name: (args.installed_mod / name).read_bytes(), args.engine_version)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
