import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import adventure_holdout_qualification as qualification
from adventure_bridge_qualification import digest
from test_decision_holdout_evidence import log as unknown_holdout_log

ROOT = Path(__file__).resolve().parents[1]


def suite_xml(names=qualification.CASES):
    return '<testsuites failures="0" errors="0" disabled="0"><testsuite>' + ''.join(
        '<testcase classname="' + name.split('.')[0] + '" name="' + name.split('.')[1]
        + '" status="run" result="completed"/>' for name in names) + '</testsuite></testsuites>'


class HoldoutQualificationTest(unittest.TestCase):
    def test_exact_nineteen_names_filter_and_original_suites_are_separate(self):
        expected = tuple(f'IndependentServer/ForcedDecisionHoldout.ProducerThenIndependentServer/{index}' for index in range(19))
        self.assertEqual(qualification.CASES, expected)
        self.assertEqual(qualification.CASES[:13], expected[:13])
        self.assertEqual(len(qualification.CASES[13:]), 6)
        self.assertEqual(qualification.FILTER, 'IndependentServer/ForcedDecisionHoldout.*')
        self.assertEqual(qualification.EXECUTION_SECONDS, 60)
        old = json.loads((ROOT / 'CI/adventure-qualification.json').read_text())
        self.assertEqual(len(old['reference_required_tests']), 58)
        self.assertFalse(set(expected) & set(old['reference_required_tests']))
        self.assertEqual(old['reference_filter'], 'ForcedBattleReference.*:Predeclared/ForcedBattleCountSweep.*')

    def test_exact_xml_rejects_missing_duplicate_skipped_failed_and_extra_cases(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'test.xml'
            valid = suite_xml()
            path.write_text(valid)
            self.assertEqual(qualification.validate_xml(path), sorted(qualification.CASES))
            for malformed in (suite_xml(qualification.CASES[:13]),
                    suite_xml(qualification.CASES[:-1]),
                    suite_xml(qualification.CASES + qualification.CASES[:1]),
                    suite_xml(qualification.CASES[:-1] + ('ForcedBattleReference.OrdinaryMelee',)),
                    valid.replace('status="run"', 'status="notrun"', 1),
                    valid.replace('result="completed"', 'result="skipped"', 1),
                    valid.replace('errors="0"', 'errors="1"'),
                    valid.replace('disabled="0"', 'disabled="1"'),
                    valid.replace('/>', '><failure/></testcase>', 1),
                    valid.replace('/>', '><error/></testcase>', 1),
                    valid.replace('/>', '><skipped/></testcase>', 1)):
                path.write_text(malformed)
                with self.subTest(xml=malformed), self.assertRaises(ValueError):
                    qualification.validate_xml(path)

    def test_mandatory_provenance_includes_decision_consumer_and_shared_budget(self):
        sources = qualification.REQUIRED_BUILD_SOURCES
        for name in ('PlanningRiskDecision.cpp', 'PlanningRiskDecision.h', 'DecisionRiskBudget.h',
                     'PlanningRiskEvidence.h', 'PlanningRiskTable.cpp', 'PlanningRiskTable.h',
                     'PlanningBattleMovement.h'):
            self.assertIn('AI/Nullkiller2/Engine/Experimental/' + name, sources)
        self.assertIn('AI/Nullkiller2/Engine/Nullkiller.cpp', sources)
        self.assertIn('AI/Nullkiller2/AIGateway.cpp', sources)
        self.assertIn('AI/Nullkiller2/Engine/ArmyLossPolicy.h', sources)
        self.assertIn('config/ai/nk2ai/nk2ai-settings.json', sources)
        original = (ROOT / 'CI/adventure_qualification.py').read_text()
        self.assertIn('*RISK_BUILD_SOURCES]', original)

    def test_missing_or_changed_model_source_rejects_preexisting_binary(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary)
            binary = source / 'reference'
            binary.write_bytes(b'verified binary')
            for name in qualification.REQUIRED_BUILD_SOURCES:
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('verified ' + name)
            record = dict(target=qualification.TARGET, cold_fallback=False, source_commit='commit',
                restore_evidence_sha256='restore', public_data_sha256=qualification.DATA_HASH,
                compile_seconds=1, planned_objects=[], compiled_objects=[], noop_verified=True,
                executable_sha256=digest(binary), fixture_sha256={name: digest(source / name) for name in qualification.REQUIRED_BUILD_SOURCES})
            for name in qualification.REQUIRED_BUILD_SOURCES:
                missing = copy.deepcopy(record)
                missing['fixture_sha256'].pop(name)
                with self.subTest(missing=name), self.assertRaises(ValueError):
                    qualification.validate_reference(missing, binary, 'commit', 'restore', source)
            for name in qualification.RISK_BUILD_SOURCES:
                path = source / name
                original = path.read_bytes()
                path.write_text('modified after build')
                with self.subTest(changed=name), self.assertRaises(ValueError):
                    qualification.validate_reference(record, binary, 'commit', 'restore', source)
                path.write_bytes(original)


class HoldoutRunnerTest(unittest.TestCase):
    def exercise(self, mode):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, build, evidence, refs = (root / name for name in ('source', 'build', 'evidence', 'references'))
            for name in qualification.SOURCES:
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('isolated provenance fixture ' + name)
            binary = build / 'bin' / qualification.TARGET
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b'verified binary')
            restore = root / 'restore.json'
            restore.write_text(json.dumps(dict(operation='restore', unchanged_sources=1,
                verified_build_files=1, verified_external_files=0, changed_sources=[], removed_sources=[])))
            archive = root / 'data.7z'
            archive.write_bytes(b'synthetic unit-test archive')
            reference = dict(target=qualification.TARGET, cold_fallback=False, source_commit='commit',
                restore_evidence_sha256=digest(restore), public_data_sha256=qualification.DATA_HASH,
                compile_seconds=1, planned_objects=[], compiled_objects=[], noop_verified=True,
                executable_sha256=digest(binary), status='incomplete', reason='strict model gate failed',
                fixture_sha256={name: digest(source / name) for name in qualification.REQUIRED_BUILD_SOURCES})
            if mode == 'partial-build': reference['noop_verified'] = False
            if mode == 'wrong-commit': reference['source_commit'] = 'other'
            if mode == 'wrong-restore': reference['restore_evidence_sha256'] = 'other'
            if mode == 'wrong-binary': binary.write_bytes(b'partially linked')
            if mode == 'invalid-restore': restore.write_text('{"operation":"restore"}')
            refs.mkdir()
            reference_file = refs / 'qualification.json'
            reference_file.write_text(json.dumps(reference))
            original_reference = reference_file.read_bytes()
            calls = []

            def run(command, logfile, seconds, cwd):
                calls.append(command)
                self.assertEqual(command[0], str(binary))
                self.assertIn('--gtest_filter=' + qualification.FILTER, command)
                self.assertEqual(seconds, 60)
                log = unknown_holdout_log()
                if mode == 'old-only-evidence':
                    log = '\n'.join(line for line in log.splitlines() if 'family=grandelf_archer' not in line)
                logfile.write_text('invalid' if mode == 'invalid-evidence' else log)
                if mode != 'missing-xml':
                    (evidence / 'tests.xml').write_text(suite_xml(qualification.CASES[:13]) if mode == 'old-only-xml' else suite_xml())
                if mode == 'timeout': raise RuntimeError('timeout=True')
                return 0.1

            def fake_archive_digest(path):
                return qualification.DATA_HASH if path == archive and mode != 'wrong-data' else digest(path)

            argv = ['holdout', '--build', str(build), '--evidence', str(evidence),
                    '--reference-evidence', str(refs), '--cache-validation', str(restore), '--data-archive', str(archive)]
            with patch('sys.argv', argv), patch.object(qualification, '__file__', str(source / 'CI/adventure_holdout_qualification.py')), \
                 patch.object(qualification, 'digest', side_effect=fake_archive_digest), \
                 patch.object(qualification.subprocess, 'check_output', return_value='commit') as git, \
                 patch.object(qualification, 'run', side_effect=run):
                if mode == 'success': qualification.main()
                else:
                    with self.assertRaises((ValueError, RuntimeError)):
                        qualification.main()
                for call in git.call_args_list:
                    self.assertEqual(call.args[0], ['git', 'rev-parse', 'HEAD'])
            result = json.loads((evidence / 'qualification.json').read_text())
            self.assertEqual(result['status'], 'completed' if mode == 'success' else 'incomplete')
            self.assertFalse(result['build_attempted'])
            self.assertFalse(result['activation_approved'])
            self.assertEqual(reference_file.read_bytes(), original_reference)
            if mode in ('success', 'missing-xml', 'old-only-xml', 'timeout'):
                self.assertEqual(result['holdout_evidence']['counts']['unknown'], 19)
                self.assertEqual(result['model_reference_status'], 'incomplete')
            if mode not in ('success', 'missing-xml', 'old-only-xml', 'old-only-evidence', 'timeout', 'invalid-evidence'):
                self.assertEqual(calls, [])

    def test_all_unknowns_retained_without_activating_or_replacing_model_failure(self):
        self.exercise('success')

    def test_every_failed_stage_stays_failed_and_diagnostics_survive(self):
        for mode in ('partial-build', 'wrong-commit', 'wrong-restore', 'wrong-binary', 'invalid-restore',
                     'wrong-data', 'missing-xml', 'old-only-xml', 'old-only-evidence', 'invalid-evidence', 'timeout'):
            with self.subTest(mode=mode): self.exercise(mode)


if __name__ == '__main__':
    unittest.main()
