"""Launcher/configuration contract checks, not a compiled launcher or gameplay test."""

import json
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]


def read_json_with_comments(path):
    token = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', re.DOTALL)
    source = token.sub(lambda match: match[0] if match[0].startswith('"') else "", path.read_text())
    return json.loads(source)


def method(source, name):
    start = source.index("void CSettingsView::" + name + "(")
    end = source.find("\nvoid CSettingsView::", start + 1)
    return source[start:end if end != -1 else None]


class LauncherAISelectionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "launcher/settingsView/csettingsview_moc.cpp").read_text()
        cls.properties = read_json_with_comments(ROOT / "config/schemas/settings.json")["properties"]["ai"]["properties"]
        cls.entries = dict(re.findall(
            r'addItem\(tr\("([^"]+)"\), "([^"]+)"\)',
            method(cls.source, "fillValidCombatAILibraries")))

    def test_every_combat_option_is_valid_for_each_saved_role(self):
        self.assertTrue(self.entries)
        for key in ("combatAlliedAI", "combatEnemyAI", "combatNeutralAI"):
            with self.subTest(role=key):
                self.assertEqual(self.properties[key]["default"], "BattleAI")
                self.assertLessEqual(set(self.entries.values()), set(self.properties[key]["enum"]))

    def test_partial_ports_have_distinct_truthful_labels_and_ids(self):
        for backend in ("HoMM3OriginalTactics", "FHeroes2Tactics", "TUMEnchantedTactics"):
            with self.subTest(backend=backend):
                labels = [label for label, value in self.entries.items() if value == backend]
                self.assertEqual(len(labels), 1)
                self.assertIn("partial port", labels[0])
                self.assertIn("experimental", labels[0])
        for reserved in ("HoMM3Original", "FHeroes2", "TUMEnchanted"):
            self.assertNotIn(reserved, self.entries.values())
        self.assertIn("MMAI", self.entries.values())

    def test_source_derived_port_hints_state_scope_without_obsolete_feature_denials(self):
        combat = method(self.source, "fillValidCombatAILibraries")
        for backend in ("HoMM3OriginalTactics", "FHeroes2Tactics"):
            with self.subTest(backend=backend):
                option = combat.split('"' + backend + '");', 1)[1].split("#endif", 1)[0]
                hint = re.search(r'tr\("([^"\n]+)"\)', option)[1]
                for scope in ("Experimental", "bounded source-derived", "attack, movement and spell",
                              "VCMI battle rules", "Not a complete original AI or adventure planner",
                              "full original-game parity is not established"):
                    self.assertIn(scope, hint)
                self.assertIn("Qt::ToolTipRole", option.split("addItem", 1)[0])
                self.assertNotIn("not implemented", hint)
                catalogs = ET.parse(ROOT / "launcher/translation/russian.ts")
                context = next(c for c in catalogs.findall("context") if c.findtext("name") == "CSettingsView")
                messages = [m for m in context.findall("message") if m.findtext("source") == hint]
                self.assertEqual(len(messages), 1)
                self.assertTrue(messages[0].findtext("oldsource"))
                self.assertIn("не подтверждено", messages[0].findtext("translation"))
                self.assertIsNone(messages[0].find("translation").get("type"))

    def test_launcher_ui_and_translation_catalogs_are_well_formed(self):
        ET.parse(ROOT / "launcher/settingsView/csettingsview_moc.ui")
        for catalog in sorted((ROOT / "launcher/translation").glob("*.ts")):
            with self.subTest(catalog=catalog.name):
                self.assertEqual(ET.parse(catalog).getroot().tag, "TS")

    def test_each_role_persists_the_item_id_instead_of_its_display_label(self):
        for widget, key in (("Friendly", "combatAlliedAI"), ("Enemy", "combatEnemyAI"), ("Neutral", "combatNeutralAI")):
            with self.subTest(role=key):
                handler = method(self.source, f"on_comboBox{widget}AI_currentIndexChanged")
                self.assertIn(f'ui->comboBox{widget}AI->itemData(index).toString()', handler)
                self.assertIn(f'settings.write["ai"]["{key}"]', handler)
                self.assertIn('node->String() = aiName.toUtf8().data()', handler)
                self.assertNotIn('currentText()', handler)


if __name__ == "__main__":
    unittest.main()
