import unittest
from incremental_diagnostics import differences


class DiagnosticTests(unittest.TestCase):
    def test_mode_content_inventory_and_ignored_mtime(self):
        a={'same': {'kind':'file','sha256':'a','mode':420,'mtime_ns':1}, 'mode':{'mode':420},'removed':{'mode':420}}
        b={'same': {'kind':'file','sha256':'a','mode':420,'mtime_ns':9}, 'mode':{'mode':493},'added':{'mode':420}}
        d=differences(a,b)
        self.assertEqual(d['missing'],['removed']);self.assertEqual(d['added'],['added']);self.assertEqual(list(d['changed']),['mode'])
        self.assertEqual(a['same']['mtime_ns'],1)


if __name__ == '__main__': unittest.main()
