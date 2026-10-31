#!/usr/bin/env python3
"""Validate four-provider battle evidence. No report means NOT accepted.

Reference traces must come from independent runs of the pinned upstream code.
This checker verifies trace consistency, not the authenticity of the runner.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
from typing import Any

PROVIDERS = {"HoMM3Original", "FHeroes2", "TUMEnchanted", "MMAI"}
SCENARIOS = {"melee", "ranged", "spells", "siege"}


def digest(value: Any, width: int) -> bool:
    return isinstance(value, str) and re.fullmatch(rf"[0-9a-f]{{{width}}}", value) is not None


def load_trace(root: Path, description: dict[str, Any]) -> list[dict[str, Any]]:
    path = (root / description["path"]).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError("Trace path escapes evidence directory")
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != description["sha256"]:
        raise ValueError(f"Trace checksum mismatch: {path.name}")
    rows = [json.loads(line) for line in raw.decode("utf-8").splitlines() if line.strip()]
    if not rows or any(not isinstance(row, dict) for row in rows):
        raise ValueError("Empty or malformed battle trace")
    return rows


def decisions(rows: list[dict[str, Any]], provider: str, revision: str, initial: str,
              ruleset: str, seed: int) -> list[tuple[str, Any]]:
    if [row.get("seq") for row in rows] != list(range(1, len(rows) + 1)):
        raise ValueError("Battle trace sequence is incomplete")
    header = rows[0]
    if (header.get("event") != "backend_loaded" or header.get("provider") != provider
            or header.get("source_revision") != revision
            or header.get("initial_state_sha256") != initial
            or header.get("ruleset") != ruleset or header.get("seed") != seed):
        raise ValueError("Loaded backend, state, ruleset or seed does not match the report")
    if provider == "MMAI" and not digest(header.get("model_sha256"), 64):
        raise ValueError("MMAI requires evidence of the loaded model")
    result = []
    applied_damage = 0
    finished = []
    for row in rows:
        if row.get("event") in {"fallback", "unsupported", "error", "timeout"} or row.get("fallback") is True:
            raise ValueError("Backend fallback, unsupported mechanics or failed execution")
        if row.get("event") == "decision":
            if row.get("provider") != provider or row.get("source_revision") != revision:
                raise ValueError("Decision made by another implementation")
            state_hash = row.get("state_sha256")
            if not digest(state_hash, 64):
                raise ValueError("Decision lacks a normalized state fingerprint")
            if row.get("action") not in row.get("legal_actions", []):
                raise ValueError("Backend produced an illegal action")
            result.append((state_hash, row["action"]))
        if row.get("event") == "damage_applied":
            amount = row.get("amount")
            if not isinstance(amount, int) or isinstance(amount, bool) or amount < 0:
                raise ValueError("Invalid applied damage")
            applied_damage += amount
        if row.get("event") == "battle_finished":
            finished.append(row)
    if not result or applied_damage <= 0 or len(finished) != 1:
        raise ValueError("No complete real battle with decisions and applied damage")
    if rows[-1] != finished[0] or finished[0].get("winner") not in {"attacker", "defender"}:
        raise ValueError("Battle did not finish normally")
    return result


def validate(report: dict[str, Any], root: Path) -> list[str]:
    errors: list[str] = []
    if not isinstance(report, dict) or report.get("schema") != 1:
        return ["Unsupported evidence schema"]
    providers = report.get("providers")
    if not isinstance(providers, list):
        return ["Missing provider evidence"]
    if any(not isinstance(entry, dict) or not isinstance(entry.get("id"), str) for entry in providers):
        return ["Malformed provider identity"]
    ids = [entry["id"] for entry in providers]
    if len(ids) != 4 or len(providers) != 4 or set(ids) != PROVIDERS:
        errors.append("All four distinct providers must be tested")
    for entry in providers:
        try:
            provider = entry["id"]
            if provider not in PROVIDERS or entry.get("selected_in_vcmi") is not True:
                raise ValueError("Backend must be selected and instantiated by VCMI")
            revision = entry["source_revision"]
            if not digest(revision, 40) or not digest(entry.get("adapter_revision"), 40):
                raise ValueError("Source and adapter commits must be pinned")
            cases = entry["cases"]
            if not isinstance(cases, list) or {case["scenario"] for case in cases} != SCENARIOS or len(cases) != len(SCENARIOS):
                raise ValueError("Melee, ranged, spells and siege cases are required")
            for case in cases:
                initial, ruleset, seed = case["initial_state_sha256"], case["ruleset"], case["seed"]
                if not digest(initial, 64) or not isinstance(ruleset, str) or not ruleset or type(seed) is not int:
                    raise ValueError("Case requires an initial-state hash, ruleset and seed")
                if (root / case["upstream"]["path"]).resolve() == (root / case["vcmi"]["path"]).resolve():
                    raise ValueError("Upstream and VCMI require separate trace files")
                reference = load_trace(root, case["upstream"])
                adapted = load_trace(root, case["vcmi"])
                expected = decisions(reference, provider, revision, initial, ruleset, seed)
                actual = decisions(adapted, provider, revision, initial, ruleset, seed)
                if expected != actual:
                    raise ValueError(f"{case['scenario']}: decisions differ from pinned upstream")
                if reference[-1]["winner"] != adapted[-1]["winner"]:
                    raise ValueError(f"{case['scenario']}: outcome differs from upstream")
                if provider == "MMAI" and reference[0]["model_sha256"] != adapted[0]["model_sha256"]:
                    raise ValueError("MMAI reference and port loaded different models")
        except (KeyError, ValueError, TypeError, OSError) as error:
            name = entry.get("id", "unknown") if isinstance(entry, dict) else "invalid"
            errors.append(f"{name}: {error}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    try:
        report = json.loads(args.report.read_text(encoding="utf-8"))
        errors = validate(report, args.report.parent)
    except (OSError, ValueError, TypeError) as error:
        errors = [str(error)]
    print(json.dumps({"accepted": not errors, "errors": errors}, indent=2))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
