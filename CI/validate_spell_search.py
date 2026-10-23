#!/usr/bin/env python3
"""Validate bounded real-spell probe artifacts. Not a latency acceptance threshold."""
import argparse
import json
import math
import re
import xml.etree.ElementTree as ET
from pathlib import Path

CONFIG = "SPELL_SEARCH_CONFIG\ttrace=off\tmode=passive\tlevel=expert\trounds=1\tcell_cap_ms=60000\ttotal_cap_ms=300000\ttarget_cap=8192\tpasses=1\tschema=2\taudit=complete_domain\tteleport_target_cap=8"
EXPECTED = {(n, spell) for n in (6, 14, 28)
            for spell in ("magicArrow", "teleport", "removeObstacle")}
DECLARATIONS = {
    "obstacle_entries", "source_units", "friendly_units", "hex_domain",
    "raw_combination_bound", "legal_pool", "selected", "pruned",
    "selection_mode", "legal_checked", "selected_in_pool", "selected_unique",
    "legality_audit_ms", "cell_ms",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def metadata(fields, expected, context):
    result = {}
    for field in fields:
        require(field.count("=") == 1, f"Malformed declaration in {context}: {field}")
        key, value = field.split("=", 1)
        require(key in expected and key not in result and value != "",
                f"Unexpected/duplicate/empty declaration in {context}: {key}")
        result[key] = value
    require(set(result) == expected, f"Missing declarations in {context}: {expected - set(result)}")
    return result


def counter(value, context):
    require(re.fullmatch(r"0|[1-9][0-9]*", value) is not None,
            f"Invalid nonnegative integer in {context}: {value}")
    return int(value)


def validate(log_path, xml_path, revision):
    require(revision in ("baseline", "candidate"), "Unknown revision")
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
    endings = [line for line in lines if line.startswith("SPELL_SEARCH_END")]
    require(len(endings) == 1, "Missing or duplicate probe completion footer")
    end_fields = endings[0].split("\t")
    require(len(end_fields) == 4 and end_fields[0] == "SPELL_SEARCH_END", "Malformed completion footer")
    end = metadata(end_fields[1:], {"cells", "total_ms", "status"}, "completion footer")
    total_ms = float(end["total_ms"])
    require(end["cells"] == "9" and end["status"] == "COMPLETE", "Incomplete/capped probe footer")
    require(math.isfinite(total_ms) and 0 <= total_ms < 300000,
            "Probe exceeded total operational cap or reported invalid total time")
    cells = {}
    for line in lines:
        if not line.startswith("SPELL_SEARCH\t"):
            require(not line.startswith("SPELL_SEARCH") or line in (CONFIG, endings[0]),
                    "Unrecognized spell-search record")
            continue
        fields = line.split("\t")
        require(len(fields) == 25, "Probe row must have 25 TSV fields")
        stacks, spell = counter(fields[1], "stacks"), fields[2]
        key = (stacks, spell)
        require(key in EXPECTED and key not in cells, f"Unexpected/duplicate cell {key}")
        target_count, evaluated = (counter(value, f"target/evaluation count in {key}")
                                   for value in fields[3:5])
        require(fields[5] == "COMPLETE", f"Incomplete/capped cell {key}: {fields[5]}")
        enumeration_ms, evaluation_ms, checksum = map(float, fields[6:9])
        changed, arity = (counter(value, f"changed/arity count in {key}")
                          for value in fields[9:11])
        declarations = metadata(fields[11:], DECLARATIONS, key)
        counts = {name: counter(value, f"{name} in {key}")
                  for name, value in declarations.items()
                  if name not in ("selection_mode", "legality_audit_ms", "cell_ms")}
        raw_bound = stacks * 187 if spell == "teleport" else 187 if spell == "removeObstacle" else stacks
        expected_sources = {"obstacle_entries": 3, "source_units": stacks,
                            "friendly_units": stacks // 2, "hex_domain": 187,
                            "raw_combination_bound": raw_bound, "legal_checked": raw_bound}
        require(all(counts[name] == value for name, value in expected_sources.items()),
                f"Source/domain/audit declaration mismatch in {key}")
        require(arity == (2 if spell == "teleport" else 1), f"Wrong target signature in {key}")
        legal_pool, selected, pruned = (counts[name] for name in ("legal_pool", "selected", "pruned"))
        require(0 < legal_pool <= min(8192, raw_bound), f"Legal pool invalid in {key}")
        require(0 <= target_count <= legal_pool and evaluated == target_count == selected,
                f"Target/selected/evaluation counts invalid in {key}")
        require(counts["selected_in_pool"] == counts["selected_unique"] == selected,
                f"Selected targets must be unique members of the audited legal pool in {key}")
        require(0 <= changed <= evaluated, f"Mutation count invalid in {key}")
        legality_audit_ms, cell_ms = (float(declarations[name])
                                      for name in ("legality_audit_ms", "cell_ms"))
        require(all(math.isfinite(value) for value in
                    (enumeration_ms, evaluation_ms, checksum, legality_audit_ms, cell_ms)),
                f"Nonfinite result in {key}")
        require(enumeration_ms >= 0 and evaluation_ms >= 0 and legality_audit_ms >= 0
                and 0 <= cell_ms < 60000, f"Cell exceeded operational cap or invalid time: {key}")
        require(legality_audit_ms + enumeration_ms + evaluation_ms <= cell_ms + 1e-6,
                f"Cell time does not include audit/enumeration/evaluation in {key}")
        unsupported = revision == "baseline" and spell != "magicArrow"
        mode = declarations["selection_mode"]
        if unsupported:
            require(mode == "empty_enumerator" and selected == changed == pruned == 0 and checksum == 0,
                    f"Pinned baseline unsupported enumeration mislabeled as pruning/support in {key}")
        else:
            require(selected > 0 and changed > 0, f"Spell did not exercise a real effect in {key}")
            require(legal_pool == selected + pruned, f"Legal/selected/pruned counts disagree in {key}")
            if spell == "teleport":
                require(mode == "teleport_k8" and selected <= 8,
                        f"Teleport shortlist must contain at most eight targets in {key}")
            else:
                require(mode == "full_enumeration" and pruned == 0 and selected == legal_pool,
                        f"Scalar/obstacle enumeration lost legal targets in {key}")
        cells[key] = {"id": f"{stacks}/{spell}", "stacks": stacks, "spell": spell,
                      "targets": target_count, "evaluated": evaluated, "changed": changed,
                      "arity": arity, "enumeration_ms": enumeration_ms,
                      "evaluation_ms": evaluation_ms, "checksum": checksum,
                      "legality_audit_ms": legality_audit_ms, "cell_ms": cell_ms,
                      "selection_mode": mode, "baseline_unsupported": unsupported,
                      "status": fields[5], **counts}
    require(set(cells) == EXPECTED, f"Missing cells: {sorted(EXPECTED - set(cells))}")
    require(sum(cell["cell_ms"] for cell in cells.values()) <= total_ms + 1e-6,
            "Total time does not include all cells")
    probe_records = [line for line in lines if line.startswith("SPELL_SEARCH")]
    require(probe_records[0] == CONFIG and probe_records[-1] == endings[0],
            "Configuration/footer must surround all probe rows")
    return {"schema": "vcmi-spell-search-v2", "revision": revision, "complete": True,
            "cells": [cells[key] for key in sorted(cells)], "total_ms": total_ms,
            "limits": {"cell_ms": 60000, "total_ms": 300000, "targets": 8192,
                       "teleport_selected": 8},
            "note": "One pass per cell. Legal pool is independently audited; Teleport evaluates only its shortlist, not a full optimum. Baseline unsupported cells are not pruning or latency speedups. Operational caps are not hard per-call or gameplay latency guarantees."}


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
