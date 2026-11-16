"""Failure-path tests use synthetic evidence, never substitute for engine runs."""
import copy
import hashlib
import io
import json
from pathlib import Path
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import reference as ref


class ReferenceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def archive(self, name='fheroes2-pin/a.txt', kind=tarfile.REGTYPE):
        archive = self.root / 'source.tar.gz'
        with tarfile.open(archive, 'w:gz') as bundle:
            member = tarfile.TarInfo(name)
            member.type = kind
            if kind == tarfile.REGTYPE:
                member.size = 3
                bundle.addfile(member, io.BytesIO(b'abc'))
            else:
                member.linkname = 'a.txt'
                bundle.addfile(member)
        lock = {'commit': 'pin', 'archive_sha256': ref.file_hash(archive),
                'files': {'a.txt': hashlib.sha1(b'blob 3\0abc').hexdigest()}}
        return archive, lock

    def test_extracts_and_verifies_every_source_blob(self):
        archive, lock = self.archive()
        rows = ref.extract_source(archive, self.root / 'source', lock)
        self.assertEqual(rows[0]['sha256'], ref.digest(b'abc'))

    def test_rejects_archive_hash_mismatch(self):
        archive, lock = self.archive()
        lock['archive_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            ref.extract_source(archive, self.root / 'source', lock)
        self.assertFalse((self.root / 'source').exists())

    def test_rejects_archive_traversal_and_links(self):
        for name, kind in [('fheroes2-pin/../escape', tarfile.REGTYPE), ('/absolute', tarfile.REGTYPE),
                           ('fheroes2-pin/a.txt', tarfile.SYMTYPE)]:
            with self.subTest(name=name, kind=kind):
                archive, lock = self.archive(name, kind)
                with self.assertRaises(ValueError):
                    ref.extract_source(archive, self.root / 'source', lock)

    def test_rejects_source_changes_and_extra_files(self):
        archive, lock = self.archive()
        source = self.root / 'source'
        ref.extract_source(archive, source, lock)
        (source / 'a.txt').write_bytes(b'bad')
        with self.assertRaisesRegex(ValueError, 'source changed'):
            ref.verify_source(source, lock)
        (source / 'a.txt').write_bytes(b'abc')
        (source / 'extra').write_bytes(b'abc')
        with self.assertRaisesRegex(ValueError, 'file set'):
            ref.verify_source(source, lock)

    def trace(self, scenario):
        units = [{'uid': 1, 'army_color': 1, 'current_color': 1, 'head': 0, 'tail': -1, 'shots': 10},
                 {'uid': 2, 'army_color': 2, 'current_color': 2, 'head': 2, 'tail': -1, 'shots': 0}]
        hero = {'control': 1, 'mana': 100, 'spells': [3]}
        rows = [{'event': i, 'phase': p, 'ambient_rng': {'state': 7}, 'units': copy.deepcopy(units),
                 'commanders': [hero, None]} for i, p in enumerate(
                 ('initial', 'planner_before', 'planner_after', 'action_before', 'action_after', 'final'))]
        rows[1]['active_uid'] = rows[2]['active_uid'] = 1
        command = {'type': 'ATTACK', 'constructor_args': [1, 2]}
        if scenario in ('spell_damage', 'spell_control'):
            command = {'type': 'SPELLCAST', 'constructor_args': [3 if scenario == 'spell_damage' else 32, 2]}
        rows[3]['command'] = command
        if scenario == 'spell_control':
            rows[4]['units'][1]['current_color'] = 1
        if scenario == 'archers':
            rows[4]['units'][0]['shots'] = 9
        return rows

    def test_observes_all_four_source_properties(self):
        for scenario in ('spell_damage', 'spell_control', 'human_auto', 'archers'):
            self.assertTrue(all(ref.trace_properties(scenario, self.trace(scenario))['checks'].values()))

    def test_rejects_missing_engine_activity(self):
        rows = self.trace('melee')
        rows[1]['phase'] = rows[2]['phase'] = 'missing'
        with self.assertRaisesRegex(ValueError, 'planner did not execute'):
            ref.trace_properties('melee', rows)

    def test_rejects_rng_mutation_and_bad_event_indices(self):
        for key, value in [('ambient_rng', {'state': 8}), ('event', 70)]:
            rows = self.trace('archers')
            rows[3][key] = value
            with self.assertRaises(ValueError):
                ref.trace_properties('archers', rows)

    def test_rejects_missing_control_effect(self):
        rows = self.trace('spell_control')
        rows[4]['units'][1]['current_color'] = 2
        with self.assertRaisesRegex(ValueError, 'Source property failed'):
            ref.trace_properties('spell_control', rows)

    def xml(self):
        mapping = ref.read_json(ref.HERE / 'properties.json')
        tests = [v['native_test'] for v in mapping['properties'].values()]
        xml = '<testsuites tests="4" failures="0" errors="0" disabled="0"><testsuite>'
        xml += ''.join(f'<testcase classname="AIIntegrationNativeBattle" name="{name}" status="run" result="completed"/>' for name in tests)
        return mapping, xml + '</testsuite></testsuites>'

    def test_native_xml_requires_exact_four_completed_tests(self):
        mapping, xml = self.xml()
        path = self.root / 'native.xml'
        path.write_text(xml)
        self.assertTrue(all(ref.xml_results(path, mapping).values()))
        path.write_text(xml.replace('status="run"', 'status="notrun"', 1))
        self.assertFalse(all(ref.xml_results(path, mapping).values()))
        path.write_text(xml.replace('result="completed"/>', 'result="completed"><skipped/></testcase>', 1))
        self.assertFalse(all(ref.xml_results(path, mapping).values()))
        path.write_text(xml.replace('completesRangedBattleThroughFactory', 'unknownTest'))
        with self.assertRaisesRegex(ValueError, 'Missing, duplicate'):
            ref.xml_results(path, mapping)

    def test_normalizes_recorded_native_source_without_claiming_checkout(self):
        mapping, xml = self.xml()
        source = mapping['native_source']
        path = self.root / 'native.xml'
        path.write_text(xml.replace('<testcase ', '<testcase file="/original-build/' + source + '" '))
        paths = ref.normalize_native_xml(path, source)
        self.assertNotIn('/original-build/', path.read_text())
        self.assertTrue(all(ref.xml_results(path, mapping).values()))
        self.assertEqual(ref.sanitize('/original-build/' + source, paths), '<compiled-source>/' + source)
        path.write_text(xml.replace('<testcase ', '<testcase file="other.cpp" '))
        with self.assertRaisesRegex(ValueError, 'Unexpected native test source'):
            ref.normalize_native_xml(path, source)

    def evidence(self):
        src, native = self.root / 'source', self.root / 'native'
        (src / 'corpus').mkdir(parents=True)
        native.mkdir()
        identity = {'status': 'clean_checkout', 'commit': '1' * 40, 'tree': '2' * 40}
        lock = ref.read_json(ref.HERE / 'upstream.json')
        mapping, xml = self.xml()
        (native / 'native.xml').write_text(xml)
        results = []
        for scenario in ref.SCENARIOS:
            for seed in ref.SEEDS:
                rows = self.trace(scenario)
                trace = ('\n'.join(json.dumps(r) for r in rows) + '\n').encode()
                for mode in ('baseline', 'observed-a', 'observed-b'):
                    stem = f'{scenario}-{seed}-{mode}'
                    (src / 'corpus' / (stem + '.json')).write_bytes(b'{}')
                    if mode != 'baseline':
                        (src / 'corpus' / (stem + '.jsonl')).write_bytes(trace)
                results.append({'scenario': scenario, 'seed': seed, 'properties': ref.trace_properties(scenario, rows),
                                'outcome_sha256': ref.digest(b'{}'), 'trace_sha256': ref.digest(trace)})
        common = {'status': 'passed', 'checkout': identity, 'harness': ref.harness_hashes(), 'upstream_commit': lock['commit']}
        source_report = dict(common, source_blobs_verified=965, original_objects_verified=249, upstream_changes=0,
                             cases=32, process_runs=96, results=results)
        tests = {v['native_test']: True for v in mapping['properties'].values()}
        (self.root / mapping['native_source']).parent.mkdir(parents=True)
        (self.root / mapping['native_source']).write_text('test fixture')
        native_report = dict(common, tests=tests, native_test_hashes={'reviewed': 'hash'},
                             native_test_file_sha256=ref.file_hash(self.root / mapping['native_source']),
                             native_xml_sha256=ref.file_hash(native / 'native.xml'))
        ref.save(src / 'verification.json', source_report)
        ref.save(native / 'native.json', native_report)
        return SimpleNamespace(source_evidence=src, native_evidence=native, repository=self.root), identity

    def paired(self, args, identity):
        report = {}
        with patch.object(ref, 'checkout_identity', return_value=identity), patch.object(ref, 'native_test_hashes', return_value={'reviewed': 'hash'}):
            ref.compare(args, report)
        return report

    def test_pairs_only_declared_properties(self):
        args, identity = self.evidence()
        report = self.paired(args, identity)
        self.assertEqual(report['status'], 'passed')
        self.assertEqual(list(report['properties'].values()), ['mapped_property_agrees'] * 4)

    def test_missing_native_evidence_is_failure_not_pass(self):
        args, identity = self.evidence()
        (args.native_evidence / 'native.json').unlink()
        with self.assertRaisesRegex(ValueError, 'evidence is missing'):
            self.paired(args, identity)

    def test_rejects_other_checkout_pin_or_harness(self):
        args, identity = self.evidence()
        path = args.native_evidence / 'native.json'
        original = ref.read_json(path)
        for key, value in [('checkout', {'status': 'local_unbound'}), ('upstream_commit', 'wrong'), ('harness', {})]:
            ref.save(path, dict(original, **{key: value}))
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.paired(args, identity)

    def test_rejects_unbound_source_and_native_even_if_identities_match(self):
        args, identity = self.evidence()
        unbound = {'status': 'local_unbound', 'commit': None, 'tree': None}
        for path in (args.source_evidence / 'verification.json', args.native_evidence / 'native.json'):
            report = ref.read_json(path)
            ref.save(path, dict(report, checkout=unbound))
        with self.assertRaisesRegex(ValueError, 'unbound evidence cannot pass'):
            self.paired(args, unbound)

    def test_native_cli_local_is_explicit_and_default_remains_strict(self):
        seen = []
        def capture(args, report):
            seen.append(args.local)
            report.update(status='passed', checkout=ref.checkout_identity(self.root, args.local) if args.local else {'status': 'clean_checkout'})
        for local in (False, True):
            work = self.root / ('local' if local else 'strict')
            argv = ['reference.py', 'native', '--repository', str(self.root),
                    '--test-binary', 'supplied-vcmitest', '--work-dir', str(work)]
            if local:
                argv.append('--local')
            with patch('sys.argv', argv), patch.object(ref, 'run_native', side_effect=capture):
                self.assertEqual(ref.main(), 0)
            report = ref.read_json(work / 'native.json')
            if local:
                self.assertEqual(report['checkout'], {'status': 'local_unbound', 'commit': None, 'tree': None})
        self.assertEqual(seen, [False, True])

    def test_rejects_changed_outcome_trace_or_xml(self):
        args, identity = self.evidence()
        for path in (args.source_evidence / 'corpus/melee-1-baseline.json',
                     args.source_evidence / 'corpus/melee-1-observed-a.jsonl', args.native_evidence / 'native.xml'):
            original = path.read_bytes()
            path.write_bytes(original + b' ')
            with self.subTest(path=path.name), self.assertRaises(ValueError):
                self.paired(args, identity)
            path.write_bytes(original)

    def test_rejects_missing_and_duplicate_source_cases(self):
        args, identity = self.evidence()
        path = args.source_evidence / 'verification.json'
        original = ref.read_json(path)
        for results in (original['results'][:-1], [original['results'][0]] * 32):
            ref.save(path, dict(original, results=results))
            with self.assertRaisesRegex(ValueError, 'Missing or duplicate'):
                self.paired(args, identity)

    def test_preserves_existing_work_directory(self):
        saved = self.root / 'evidence/status.json'
        ref.save(saved, {'original': True})
        before = saved.read_bytes()
        with patch('sys.argv', ['reference.py', 'source', '--local', '--work-dir', str(self.root)]):
            self.assertEqual(ref.main(), 1)
        self.assertEqual(saved.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
