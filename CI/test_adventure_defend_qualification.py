import copy
import json
from pathlib import Path
import re
import tempfile
import unittest
from unittest.mock import patch
import adventure_defend_qualification as qualification
from adventure_bridge_qualification import digest

ROOT = Path(__file__).resolve().parents[1]


def suite_xml(names=qualification.CASES):
    return '<testsuites failures="0" errors="0" disabled="0"><testsuite>' + ''.join(
        '<testcase classname="' + name.split('.')[0] + '" name="' + name.split('.')[1]
        + '" status="run" result="completed"/>' for name in names) + '</testsuite></testsuites>'


class DefendReferenceIdentityTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.executable = self.root / 'reference'
        self.executable.write_bytes(b'verified native binary')
        for name in qualification.REQUIRED_BUILD_SOURCES:
            fixture = self.root / name
            fixture.parent.mkdir(parents=True, exist_ok=True)
            fixture.write_text('reviewed fixture ' + name)
        self.reference = dict(status='incomplete', reason='supported model smoke failed',
            target=qualification.TARGET, cold_fallback=False, source_commit='commit',
            restore_evidence_sha256='restore', public_data_sha256=qualification.DATA_HASH,
            planned_objects=[], compiled_objects=[], compile_seconds=1.0, noop_verified=True,
            executable_sha256=digest(self.executable),
            fixture_sha256={name: digest(self.root / name) for name in qualification.REQUIRED_BUILD_SOURCES})

    def validate(self, reference):
        return qualification.validate_reference(reference, self.executable, 'commit', 'restore', self.root)

    def test_failed_model_can_reuse_verified_binary_without_mutating_failure(self):
        before = copy.deepcopy(self.reference)
        self.assertEqual(self.validate(self.reference), digest(self.executable))
        self.assertEqual(before, self.reference)

    def test_partial_build_or_missing_noop_cannot_reuse_binary(self):
        for update in ({'compile_seconds': None}, {'compile_seconds': True}, {'compile_seconds': -1},
                       {'compile_seconds': float('nan')}, {'compile_seconds': float('inf')},
                       {'planned_objects': None}, {'compiled_objects': None},
                       {'noop_verified': False}, {'noop_verified': 1}, {'executable_sha256': 'wrong'}):
            with self.subTest(update=update), self.assertRaises(ValueError):
                self.validate(dict(self.reference, **update))

    def test_changed_commit_restore_data_target_or_source_rejected(self):
        for update in ({'source_commit': 'other'}, {'restore_evidence_sha256': 'other'},
                       {'target': 'vcmitest'}, {'public_data_sha256': 'other'},
                       {'cold_fallback': True}, {'fixture_sha256': {}},
                       {'fixture_sha256': {'test/battle/ForcedBattleReference.cpp': 'wrong'}}):
            with self.subTest(update=update), self.assertRaises(ValueError):
                self.validate(dict(self.reference, **update))
        self.executable.unlink()
        with self.assertRaisesRegex(ValueError, 'missing or changed'):
            self.validate(self.reference)

    def test_invalid_fixture_paths_are_not_read(self):
        for bad_path in ('/outside.cpp', '../outside.cpp'):
            reference = copy.deepcopy(self.reference)
            reference['fixture_sha256'][bad_path] = 'unknown'
            with self.assertRaisesRegex(ValueError, 'Reference source changed'):
                self.validate(reference)

    def test_missing_or_changed_defend_header_cannot_reuse_old_binary(self):
        header = 'AI/Nullkiller2/Engine/Experimental/DetachedDefendBonuses.h'
        reference = copy.deepcopy(self.reference)
        reference['fixture_sha256'].pop(header)
        with self.assertRaisesRegex(ValueError, 'fixture identity is missing'):
            self.validate(reference)
        (self.root / header).write_text('changed after successful no-op')
        with self.assertRaisesRegex(ValueError, 'Reference source changed'):
            self.validate(self.reference)


class DefendXmlTest(unittest.TestCase):
    def validate(self, text):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'tests.xml'
            path.write_text(text)
            return qualification.validate_xml(path)

    def test_exact_three_completed_cases_only(self):
        valid = suite_xml()
        self.assertEqual(self.validate(valid), sorted(qualification.CASES))
        for xml in (suite_xml(qualification.CASES[:2]), suite_xml(qualification.CASES + qualification.CASES[:1]),
                    suite_xml(qualification.CASES[:2] + ('ForcedBattleReference.OrdinaryMelee',)),
                    valid.replace('status="run"', 'status="notrun"', 1),
                    valid.replace('result="completed"', 'result="skipped"', 1),
                    valid.replace('failures="0"', 'failures="1"'),
                    valid.replace('errors="0"', 'errors="1"'),
                    valid.replace('disabled="0"', 'disabled="1"'),
                    valid.replace('/>', '><failure/></testcase>', 1),
                    valid.replace('/>', '><error/></testcase>', 1),
                    valid.replace('/>', '><skipped/></testcase>', 1), '<testsuites/>'):
            with self.subTest(xml=xml), self.assertRaises(ValueError):
                self.validate(xml)

    def test_exact_fixture_names_and_original_matrix_remain_separate(self):
        fixture = (ROOT / 'test/battle/ForcedBattleReference.cpp').read_text()
        actual = re.findall(r'TEST_F\(ForcedDefendReference,\s*(\w+)\)', fixture)
        self.assertEqual(set(qualification.CASES), {'ForcedDefendReference.' + name for name in actual})
        config = json.loads((ROOT / 'CI/adventure-qualification.json').read_text())
        self.assertEqual(len(config['reference_required_tests']), 58)
        self.assertEqual(config['reference_filter'], 'ForcedBattleReference.*:Predeclared/ForcedBattleCountSweep.*')
        self.assertFalse(set(qualification.CASES) & set(config['reference_required_tests']))
        from forced_battle_evidence import REQUIRED_SMOKES
        self.assertEqual(REQUIRED_SMOKES, ('ordinary_melee', 'ranged_slow_melee', 'undead_melee', 'neutral_0_0'))


class DefendRunnerTest(unittest.TestCase):
    def exercise(self, mode):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build, evidence, refs = root / 'build', root / 'evidence', root / 'references'
            binary = build / 'bin' / qualification.TARGET
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b'verified binary')
            restore = root / 'restore.json'
            restore.write_text(json.dumps(dict(operation='restore', unchanged_sources=1,
                verified_build_files=1, verified_external_files=0, changed_sources=[], removed_sources=[])))
            archive = root / 'data.7z'
            archive.write_bytes(b'test archive')
            reference = dict(target=qualification.TARGET, cold_fallback=False, source_commit='commit',
                restore_evidence_sha256=digest(restore), public_data_sha256=digest(archive),
                compile_seconds=1, planned_objects=[], compiled_objects=[], noop_verified=True,
                executable_sha256=digest(binary), status='incomplete', reason='model failed',
                fixture_sha256={name: digest(ROOT / name) for name in qualification.REQUIRED_BUILD_SOURCES})
            if mode == 'partial-build':
                reference['noop_verified'] = False
            refs.mkdir()
            reference_file = refs / 'qualification.json'
            reference_file.write_text(json.dumps(reference))
            original = reference_file.read_bytes()
            calls = []

            def run(command, logfile, seconds, cwd):
                calls.append(command)
                self.assertEqual(command[0], str(binary))
                self.assertIn('--gtest_filter=ForcedDefendReference.*', command)
                self.assertEqual(seconds, 60)
                logfile.write_text('native controls')
                if mode != 'missing-xml':
                    (evidence / 'tests.xml').write_text(suite_xml())
                if mode == 'timeout':
                    raise RuntimeError('timeout=True')
                if mode == 'changed-binary':
                    binary.write_bytes(b'changed during execution')
                return 0.1

            argv = ['defend', '--build', str(build), '--evidence', str(evidence),
                    '--reference-evidence', str(refs), '--cache-validation', str(restore), '--data-archive', str(archive)]
            with patch('sys.argv', argv), patch.object(qualification, 'DATA_HASH', digest(archive)), \
                 patch.object(qualification.subprocess, 'check_output', return_value='commit') as git, \
                 patch.object(qualification, 'run', side_effect=run):
                if mode == 'success':
                    qualification.main()
                else:
                    with self.assertRaises((ValueError, RuntimeError, FileNotFoundError)):
                        qualification.main()
                self.assertEqual(git.call_args.args[0], ['git', 'rev-parse', 'HEAD'])
                self.assertEqual(git.call_count, 1)
            result = json.loads((evidence / 'qualification.json').read_text())
            self.assertEqual(result['status'], 'completed' if mode == 'success' else 'incomplete')
            self.assertFalse(result['build_attempted'])
            self.assertEqual(result['model_reference_status'], 'incomplete')
            self.assertEqual(result['model_reference_reason'], 'model failed')
            self.assertEqual(reference_file.read_bytes(), original)
            self.assertEqual(len(calls), 0 if mode == 'partial-build' else 1)

    def test_failed_model_does_not_block_independent_exact_defend_gate(self):
        self.exercise('success')

    def test_partial_build_cannot_launch_and_execution_failures_do_not_pass(self):
        for mode in ('partial-build', 'missing-xml', 'timeout', 'changed-binary'):
            with self.subTest(mode=mode):
                self.exercise(mode)


if __name__ == '__main__':
    unittest.main()
