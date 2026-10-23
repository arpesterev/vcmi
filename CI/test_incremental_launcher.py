import pathlib
import shutil
import subprocess
import tempfile
import unittest
from incremental_launcher import validate


class LauncherTests(unittest.TestCase):
    def test_inline_and_quoted_paths(self):
        self.assertEqual(validate('"/some path/ccache" /usr/bin/c++ -o obj.o -c file.cpp\nccache cc -c c.c -o c.o'), 2)

    def test_missing_and_unused_launcher(self):
        for commands in ('c++ -o obj.o -c f.cpp', 'echo ccache\nc++ -o obj.o -c f.cpp', ''):
            with self.assertRaises(ValueError):
                validate(commands)

    @unittest.skipUnless(shutil.which('cmake') and shutil.which('ninja'), 'requires CMake and Ninja')
    def test_real_cmake_expanded_launcher(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / 'unit.cpp').write_text('int unit() { return 1; }\n')
            (root / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\nproject(launcher LANGUAGES CXX)\nadd_library(unit OBJECT unit.cpp)\nset_property(TARGET unit PROPERTY CXX_COMPILER_LAUNCHER "${TEST_LAUNCHER}")\n')
            for launcher in ('/usr/bin/ccache', ''):
                subprocess.run(['cmake', '-S', tmp, '-B', str(root / 'build'), '-G', 'Ninja', '-DTEST_LAUNCHER=' + launcher], check=True, stdout=subprocess.DEVNULL)
                commands = subprocess.check_output(['ninja', '-C', str(root / 'build'), '-t', 'commands', 'unit'], text=True)
                if launcher:
                    self.assertEqual(validate(commands), 1)
                else:
                    with self.assertRaises(ValueError):
                        validate(commands)


if __name__ == '__main__':
    unittest.main()
