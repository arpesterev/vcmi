import unittest
from incremental_diagnostics import differences


class DiagnosticTests(unittest.TestCase):
    def test_mode_content_inventory_and_ignored_mtime(self):
        a={'same': {'kind':'file','sha256':'a','mode':420,'mtime_ns':1}, 'mode':{'mode':420},'removed':{'mode':420}}
        b={'same': {'kind':'file','sha256':'a','mode':420,'mtime_ns':9}, 'mode':{'mode':493},'added':{'mode':420}}
        d=differences(a,b)
        self.assertEqual(d['missing'],['removed']);self.assertEqual(d['added'],['added']);self.assertEqual(list(d['changed']),['mode'])
        self.assertEqual(a['same']['mtime_ns'],1)


class TarModeTests(unittest.TestCase):
    def test_gnu_tar_preserves_manifest_modes(self):
        import os
        import pathlib
        import shutil
        import stat
        import subprocess
        import tempfile
        tar = shutil.which('gtar') or shutil.which('tar')
        if not tar or 'GNU tar' not in subprocess.check_output([tar, '--version'], text=True):
            self.skipTest('GNU tar required, matching Actions cache restore')
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp); source=root/'source'; restored=root/'restored'; source.mkdir(); restored.mkdir()
            for name,mode in [('header.h',0o664),('tool',0o775)]:
                path=source/name;path.write_text('identical bytes');path.chmod(mode)
            archive=root/'cache.tar'
            subprocess.run([tar,'-cf',str(archive),'-C',str(source),'.'],check=True)
            subprocess.run([tar,'--no-same-permissions','-xf',str(archive),'-C',str(restored)],env={**os.environ,'TAR_OPTIONS':''},preexec_fn=lambda:os.umask(0o022),check=True)
            self.assertEqual(stat.S_IMODE((restored/'header.h').stat().st_mode),0o644)
            subprocess.run([tar,'-xf',str(archive),'-C',str(restored)],env={**os.environ,'TAR_OPTIONS':'--same-permissions'},preexec_fn=lambda:os.umask(0o022),check=True)
            for name,mode in [('header.h',0o664),('tool',0o775)]:
                self.assertEqual(stat.S_IMODE((restored/name).stat().st_mode),mode)
                self.assertEqual((restored/name).read_bytes(),(source/name).read_bytes())


if __name__ == '__main__': unittest.main()
