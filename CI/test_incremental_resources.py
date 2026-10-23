import pathlib
import shutil
import subprocess
import tempfile
import unittest


class ResourceTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('cmake'), 'CMake required')
    def test_same_resource_stable_changed_resource_updated(self):
        helper=pathlib.Path(__file__).resolve().parents[1]/'cmake_modules/VCMIWriteIfDifferent.cmake'
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);output=root/'translations.qrc';script=root/'write.cmake'
            def generate(content):
                script.write_text('cmake_minimum_required(VERSION 3.16)\ninclude("'+str(helper)+'")\nvcmi_write_if_different("'+str(output)+'" [==['+content+']==])\n')
                subprocess.run(['cmake','-P',str(script)],check=True)
            content='<!DOCTYPE RCC>\n<RCC version="1.0">\n<qresource prefix="/">\n<file>a.qm</file>\n<file>b.qm</file>\n\n</qresource>\n</RCC>'
            generate(content);self.assertEqual(output.read_text(),content)
            stamp=output.stat().st_mtime_ns;generate(content);self.assertEqual(output.stat().st_mtime_ns,stamp)
            changed=content.replace('b.qm','c.qm');generate(changed);self.assertEqual(output.read_text(),changed)
            self.assertNotEqual(output.stat().st_mtime_ns,stamp)


if __name__ == '__main__': unittest.main()
