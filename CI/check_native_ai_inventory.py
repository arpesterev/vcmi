#!/usr/bin/env python3
"""Check a compiled Linux inventory against the reviewed macOS workflow counts."""

import argparse
import json
import re
from pathlib import Path

from run_macos_native_ai import parse_test_inventory, verify_native_inventory


def workflow_counts(text):
    counts = {}
    for name in ("native", "forecast", "lifecycle"):
        flag = "--expected-" + name + "-count"
        values = re.findall(re.escape(flag) + r"\s+([^\s\\]+)", text)
        if len(values) != 1 or not re.fullmatch(r"[0-9]+", values[0]):
            raise ValueError("Workflow needs exactly one literal reviewed " + flag)
        counts[name] = int(values[0])
    if counts["native"] <= 0 or not 0 <= counts["lifecycle"] <= counts["forecast"]:
        raise ValueError("Workflow candidate counts are inconsistent")
    return counts


def check_inventory(listing, workflow):
    counts = workflow_counts(workflow)
    identities = parse_test_inventory(listing)
    selected = verify_native_inventory(identities, counts["native"], counts["forecast"], counts["lifecycle"])
    return {"status": "passed", "evidence": "compiled-inventory-only", "expected_counts": counts,
            "compiled_cases": len(identities), "selected_non_neural_cases": len(selected)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", required=True, type=Path)
    parser.add_argument("--mac-workflow", required=True, type=Path)
    args = parser.parse_args()
    try:
        report = check_inventory(args.inventory.read_text(encoding="utf-8"),
                                 args.mac_workflow.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        parser.exit(1, "Native candidate inventory check failed: " + str(error) + "\n")
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
