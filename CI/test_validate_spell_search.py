#!/usr/bin/env python3
"""Synthetic artifact-contract tests only; no VCMI executable or game is run."""
import tempfile
import unittest
from pathlib import Path

from validate_spell_search import CONFIG, DECLARATIONS, validate

XML = ('<testsuites tests="1" failures="0" errors="0" disabled="1">'
       '<testsuite tests="1" failures="0" errors="0" disabled="1">'
       '<testcase classname="BattleSpellSearchBenchmark" '
       'name="DISABLED_BoundedRealSpellSearch" status="run" result="completed"/>'
       '</testsuite></testsuites>')


def artifacts(revision="candidate"):
    """Invented numbers exercise parser rules, not observed engine performance."""
    rows = [CONFIG]
    for stacks in (6, 14, 28):
        for spell in ("magicArrow", "teleport", "removeObstacle"):
            unsupported = revision == "baseline" and spell != "magicArrow"
            pool = 70 if spell == "teleport" else 3
            selected = 0 if unsupported else 8 if spell == "teleport" else pool
            mode = "empty_enumerator" if unsupported else "teleport_k8" if spell == "teleport" else "full_enumeration"
            raw = stacks * 187 if spell == "teleport" else 187 if spell == "removeObstacle" else stacks
            fields = ["SPELL_SEARCH", str(stacks), spell, str(selected), str(selected),
                      "COMPLETE", "1", "2", "0" if unsupported else "4.5", str(selected),
                      "2" if spell == "teleport" else "1",
                      "obstacle_entries=3", f"source_units={stacks}",
                      f"friendly_units={stacks // 2}", "hex_domain=187",
                      f"raw_combination_bound={raw}", f"legal_pool={pool}",
                      f"selected={selected}", f"pruned={0 if unsupported else pool - selected}",
                      f"selection_mode={mode}", f"legal_checked={raw}",
                      f"selected_in_pool={selected}", f"selected_unique={selected}",
                      "legality_audit_ms=3", "cell_ms=7"]
            rows.append("\t".join(fields))
    rows.append("SPELL_SEARCH_END\tcells=9\ttotal_ms=70\tstatus=COMPLETE")
    return rows


def alter(rows, row=2, columns=None, **declarations):
    result = rows.copy()
    fields = result[row].split("\t")
    for column, value in (columns or {}).items():
        fields[column] = str(value)
    for name, value in declarations.items():
        index = next(i for i, field in enumerate(fields) if field.startswith(name + "="))
        fields[index] = f"{name}={value}"
    result[row] = "\t".join(fields)
    return result


class SpellSearchArtifactContractTest(unittest.TestCase):
    def parse(self, rows, revision="candidate", xml=XML):
        with tempfile.TemporaryDirectory(prefix="spell-search-contract-") as folder:
            log, xml_path = Path(folder) / "probe.log", Path(folder) / "probe.xml"
            log.write_text("\n".join(rows) + "\n")
            xml_path.write_text(xml)
            return validate(log, xml_path, revision)

    def reject(self, rows, revision="candidate", xml=XML):
        with self.assertRaises(ValueError):
            self.parse(rows, revision, xml)

    def test_candidate_shortlist_is_not_full_legal_pool(self):
        result = self.parse(artifacts())
        self.assertEqual(result["schema"], "vcmi-spell-search-v2")
        teleport = next(c for c in result["cells"] if c["id"] == "6/teleport")
        self.assertEqual((teleport["legal_pool"], teleport["selected"], teleport["pruned"]), (70, 8, 62))
        self.assertFalse(teleport["baseline_unsupported"])

    def test_baseline_unsupported_is_not_pruning(self):
        result = self.parse(artifacts("baseline"), "baseline")
        for cell in result["cells"]:
            if cell["spell"] != "magicArrow":
                self.assertTrue(cell["baseline_unsupported"])
                self.assertGreater(cell["legal_pool"], 0)
                self.assertEqual((cell["selected"], cell["pruned"], cell["changed"]), (0, 0, 0))
                self.assertEqual(cell["selection_mode"], "empty_enumerator")

    def test_each_metadata_field_is_required(self):
        for name in DECLARATIONS:
            with self.subTest(name=name):
                rows = artifacts()
                rows[2] = "\t".join(f for f in rows[2].split("\t") if not f.startswith(name + "="))
                self.reject(rows)

    def test_each_duplicate_metadata_field_is_rejected(self):
        for name in DECLARATIONS:
            with self.subTest(name=name):
                rows = artifacts()
                fields = rows[2].split("\t")
                existing = next(field for field in fields if field.startswith(name + "="))
                index = next(i for i in range(11, len(fields)) if not fields[i].startswith(name + "="))
                fields[index] = existing
                rows[2] = "\t".join(fields)
                self.reject(rows)

    def test_unknown_empty_and_malformed_metadata(self):
        for declaration in ("unknown=1", "legal_pool=", "legal_pool", "legal_pool=70=70"):
            with self.subTest(declaration=declaration):
                rows = artifacts()
                fields = rows[2].split("\t")
                fields[16] = declaration
                rows[2] = "\t".join(fields)
                self.reject(rows)

    def test_inconsistent_counts(self):
        for name, value in (("selected", 7), ("pruned", 61), ("legal_pool", 7),
                            ("legal_pool", 2000), ("legal_pool", 0), ("selected_unique", 7),
                            ("selected_in_pool", 7), ("legal_checked", 1121),
                            ("raw_combination_bound", 70), ("source_units", 5),
                            ("friendly_units", 2), ("hex_domain", 186), ("obstacle_entries", 2)):
            with self.subTest(name=name, value=value):
                self.reject(alter(artifacts(), **{name: value}))
        for column, value in ((3, 7), (4, 7), (9, 9), (10, 1)):
            with self.subTest(column=column):
                self.reject(alter(artifacts(), columns={column: value}))

    def test_strict_nonnegative_integers(self):
        for value in ("-1", "+8", "8.0", "08", "nan", ""):
            with self.subTest(value=value):
                self.reject(alter(artifacts(), selected=value))

    def test_ninth_teleport_target_is_rejected(self):
        self.reject(alter(artifacts(), columns={3: 9, 4: 9, 9: 9}, selected=9,
                          selected_unique=9, selected_in_pool=9, pruned=61))

    def test_small_nonempty_teleport_shortlist_is_permitted(self):
        self.parse(alter(artifacts(), columns={3: 1, 4: 1, 9: 1}, selected=1,
                         selected_unique=1, selected_in_pool=1, pruned=69))

    def test_supported_cells_need_an_effect(self):
        self.reject(alter(artifacts(), columns={9: 0}))
        self.reject(alter(artifacts(), columns={3: 0, 4: 0, 9: 0}, selected=0,
                          selected_unique=0, selected_in_pool=0, pruned=0,
                          selection_mode="empty_enumerator"))

    def test_nonteleport_requires_complete_enumeration(self):
        for row in (1, 3):
            with self.subTest(row=row):
                self.reject(alter(artifacts(), row=row, columns={3: 2, 4: 2, 9: 2},
                                  selected=2, selected_unique=2, selected_in_pool=2, pruned=1))

    def test_selection_modes_are_enforced(self):
        self.reject(alter(artifacts(), selection_mode="full_enumeration"))
        self.reject(alter(artifacts(), row=1, selection_mode="teleport_k8"))
        self.reject(alter(artifacts("baseline"), pruned=70), "baseline")
        self.reject(alter(artifacts("baseline"), selection_mode="teleport_k8"), "baseline")
        self.reject(artifacts(), "baseline")

    def test_time_and_target_caps_are_failures(self):
        for status in ("TIME_CAP", "TARGET_CAP", "PARTIAL"):
            with self.subTest(status=status):
                self.reject(alter(artifacts(), columns={5: status}))
        self.reject(alter(artifacts(), cell_ms=60000))
        self.reject(alter(artifacts(), row=10, total_ms=300000))
        self.reject(alter(artifacts(), row=10, status="TIME_CAP"))
        rows = artifacts()
        rows.append("SPELL_SEARCH_TOTAL_CAP\t300000")
        self.reject(rows)

    def test_nonfinite_numbers_are_rejected(self):
        for value in ("nan", "inf", "-inf"):
            for column in (6, 7, 8):
                with self.subTest(value=value, column=column):
                    self.reject(alter(artifacts(), columns={column: value}))
            for name in ("legality_audit_ms", "cell_ms"):
                with self.subTest(value=value, name=name):
                    self.reject(alter(artifacts(), **{name: value}))
            self.reject(alter(artifacts(), row=10, total_ms=value))

    def test_negative_times_are_rejected(self):
        self.reject(alter(artifacts(), columns={6: -1}))
        self.reject(alter(artifacts(), columns={7: -1}))
        self.reject(alter(artifacts(), legality_audit_ms=-1))
        self.reject(alter(artifacts(), cell_ms=-1))
        self.reject(alter(artifacts(), row=10, total_ms=-1))

    def test_audit_is_included_in_cell_and_total_time(self):
        self.reject(alter(artifacts(), cell_ms=5))
        self.reject(alter(artifacts(), row=10, total_ms=62))

    def test_exact_nine_cells(self):
        rows = artifacts()
        self.reject(rows[:2] + rows[3:])
        self.reject(rows[:2] + [rows[1]] + rows[3:])
        self.reject(alter(rows, columns={1: 8}))
        self.reject(alter(rows, columns={2: "otherSpell"}))

    def test_exact_configuration(self):
        rows = artifacts()
        self.reject(rows[1:])
        self.reject([CONFIG] + rows)
        self.reject([CONFIG.replace("schema=2", "schema=1")] + rows[1:])

    def test_exact_footer(self):
        rows = artifacts()
        self.reject(rows[:-1])
        self.reject(rows + [rows[-1]])
        self.reject(alter(rows, row=10, cells=8))
        self.reject(rows[:-1] + [rows[-1].replace("status=COMPLETE", "cells=9")])
        self.reject(rows[:-1] + [rows[-1] + "\textra=1"])
        self.reject(rows + ["SPELL_SEARCH_UNKNOWN\tx=1"])

    def test_record_order(self):
        rows = artifacts()
        self.reject([rows[1], rows[0]] + rows[2:])
        self.reject(rows[:-2] + [rows[-1], rows[-2]])

    def test_mock_and_error_diagnostics_are_rejected(self):
        for diagnostic in ("GMOCK WARNING", "Uninteresting mock call", "Unexpected mock call",
                           "[ERROR] broken", "TRACE: detail", "FATAL: broken", "DEBUG: detail"):
            with self.subTest(diagnostic=diagnostic):
                self.reject(artifacts() + [diagnostic])

    def test_xml_still_gates_failures_skips_and_wrong_test(self):
        for xml in (XML.replace('failures="0"', 'failures="1"', 1),
                    XML.replace('status="run"', 'status="notrun"'),
                    XML.replace('result="completed"', 'result="skipped"'),
                    XML.replace('DISABLED_BoundedRealSpellSearch', 'AnotherTest'),
                    XML.replace('/>', '><failure message="broken"/></testcase>'),
                    XML.replace('/>', '><skipped/></testcase>')):
            with self.subTest(xml=xml):
                self.reject(artifacts(), xml=xml)

    def test_old_schema_rows_are_rejected(self):
        rows = artifacts()
        rows[2] = "\t".join(rows[2].split("\t")[:16])
        self.reject(rows)


if __name__ == "__main__":
    unittest.main()
