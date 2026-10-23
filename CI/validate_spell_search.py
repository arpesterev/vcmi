#!/usr/bin/env python3
"""Validate bounded real-spell probe artifacts. Not a latency acceptance threshold."""
import argparse
import json
import math
import re
import xml.etree.ElementTree as ET
from pathlib import Path

CONFIG = "SPELL_SEARCH_CONFIG\ttrace=off\tmode=passive\tlevel=expert\trounds=1\tcell_cap_ms=60000\ttotal_cap_ms=300000\ttarget_cap=8192\tpasses=1"
EXPECTED = {(n, spell) for n in (6, 14, 28)
            for spell in ("magicArrow", "teleport", "removeObstacle")}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(log_path, xml_path, revision):
    root = ET.parse(xml_path).getroot()
    cases = list(root.iter("testcase"))
    require(len(cases) == 1, "XML must contain exactly one selected probe")
    case = cases[0]
    require(case.get("classname") == "BattleSpellSearchBenchmark"
            and case.get("name") == "DISABLED_BoundedRealSpellSearch", "Wrong XML test")
    require(case.get("status") == "run" and case.get("result") == "completed",
            "Probe was not run to completion")
    require(not any(case.findall(tag) for tag in ("failure", "error", "skipped")),
            "Probe failed or was skipped")
    # A deliberately DISABLED_ test can retain XML disabled=1 even when opted in.
    # Its run/completed status above, not that aggregate count, proves execution.
    for node in root.iter():
        for attribute in ("failures", "errors"):
            require(int(node.get(attribute, "0")) == 0,
                    f"XML {attribute} is nonzero")
    lines = Path(log_path).read_text().splitlines()
    require([line for line in lines if line.startswith("SPELL_SEARCH_CONFIG")] == [CONFIG],
            "Missing, duplicate, or unexpected probe configuration")
    require(not any(line.startswith("SPELL_SEARCH_TOTAL_CAP") for line in lines), "Total cap reached")
    require(not any(re.search(r"GMOCK WARNING|Uninteresting mock|Unexpected mock|(^|[\s\[])(TRACE|DEBUG|ERROR|FATAL)(:|\]|\s)", line, re.IGNORECASE)
                    for line in lines), "Diagnostics contaminated the probe log")
    cells = {}
    for line in lines:
        if not line.startswith("SPELL_SEARCH\t"):
            continue
        fields = line.split("\t")
        require(len(fields) == 16, "Probe row must have 16 TSV fields")
        stacks, spell = int(fields[1]), fields[2]
        key = (stacks, spell)
        require(key in EXPECTED and key not in cells, f"Unexpected/duplicate cell {key}")
        target_count, evaluated = int(fields[3]), int(fields[4])
        require(fields[5] == "COMPLETE", f"Incomplete/capped cell {key}: {fields[5]}")
        enumeration_ms, evaluation_ms, checksum = map(float, fields[6:9])
        changed, arity = int(fields[9]), int(fields[10])
        declarations = dict(field.split("=", 1) for field in fields[11:])
        raw_bound = stacks * 187 if spell == "teleport" else 187 if spell == "removeObstacle" else stacks
        require(declarations == {"obstacle_entries": "3", "source_units": str(stacks),
                                 "friendly_units": str(stacks // 2), "hex_domain": "187",
                                 "raw_combination_bound": str(raw_bound)},
                f"Source/domain declaration mismatch in {key}")
        require(arity == (2 if spell == "teleport" else 1), f"Wrong target signature in {key}")
        require(0 <= target_count <= min(8192, raw_bound) and evaluated == target_count,
                f"Target/evaluation counts invalid in {key}")
        require(0 <= changed <= evaluated, f"Mutation count invalid in {key}")
        require(all(math.isfinite(value) for value in (enumeration_ms, evaluation_ms, checksum)),
                f"Nonfinite result in {key}")
        require(enumeration_ms >= 0 and evaluation_ms >= 0
                and enumeration_ms + evaluation_ms < 60000, f"Cell exceeded operational cap: {key}")
        unsupported = revision == "baseline" and spell != "magicArrow"
        if unsupported:
            require(target_count == 0 and changed == 0, f"Pinned baseline unexpectedly supports {key}")
        else:
            require(target_count > 0 and changed > 0, f"Spell did not exercise a real effect in {key}")
        cells[key] = {"id": f"{stacks}/{spell}", "stacks": stacks, "spell": spell,
                      "targets": target_count, "evaluated": evaluated, "changed": changed,
                      "arity": arity, "enumeration_ms": enumeration_ms,
                      "evaluation_ms": evaluation_ms, "checksum": checksum,
                      "baseline_unsupported": unsupported, "status": fields[5],
                      **{key: int(value) for key, value in declarations.items()}}
    require(set(cells) == EXPECTED, f"Missing cells: {sorted(EXPECTED - set(cells))}")
    return {"schema": "vcmi-spell-search-v1", "revision": revision, "complete": True,
            "cells": [cells[key] for key in sorted(cells)],
            "limits": {"cell_ms": 60000, "total_ms": 300000, "targets": 8192},
            "note": "One pass per cell. Baseline unsupported cells are not latency speedups; bounded completion is not gameplay latency acceptance."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--xml", required=True, type=Path)
    parser.add_argument("--revision", required=True, choices=("baseline", "candidate"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = validate(args.log, args.xml, args.revision)
    except (ValueError, OSError, ET.ParseError) as error:
        parser.exit(1, f"SPELL_SEARCH_INVALID: {error}\n")
    rendered = json.dumps(result, indent=2, allow_nan=False) + "\n"
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
