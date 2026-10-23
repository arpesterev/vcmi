import json, pathlib, subprocess, sys, tempfile, unittest
class ReportTest(unittest.TestCase):
    def invoke(self, before, after, final, log='ninja: no work to do.\n'):
        with tempfile.TemporaryDirectory() as temp:
            d=pathlib.Path(temp)
            for name, value in [('ninja-before.log',before),('ninja-after.log',after),('ninja-noop.log',final),('noop.log',log),('configure-seconds.txt','1'),('build-seconds.txt','2')]: (d/name).write_text(value)
            result=subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('incremental_report.py')),'--build',temp,'--evidence',temp],capture_output=True,text=True)
            return result.returncode,json.loads((d/'incremental-report.json').read_text())
    def test_noop_pass(self):
        row='0\t2\t30\tlib/CMakeFiles/vcmi.dir/x.cpp.o\thash\n'
        status,report=self.invoke(row,row,row);self.assertEqual(status,0);self.assertEqual(report['object_edges'],0)
    def test_changed_object_counted(self):
        a='0\t2\t30\tlib/CMakeFiles/vcmi.dir/x.cpp.o\thash\n';b=a.replace('\t30\t','\t40\t')
        status,report=self.invoke(a,b,b);self.assertEqual(status,0);self.assertEqual(report['objects_by_target'],{'vcmi':1})
    def test_silent_noop_compile_rejected(self):
        a='0\t2\t30\tlib/CMakeFiles/vcmi.dir/x.cpp.o\thash\n';b=a.replace('\t30\t','\t40\t')
        status,report=self.invoke(a,a,b);self.assertNotEqual(status,0);self.assertTrue(report['noop_binary_outputs'])
    def test_silent_noop_link_rejected(self):
        a='0\t2\t30\tbin/vcmiclient\thash\n';b=a.replace('\t30\t','\t40\t')
        self.assertNotEqual(self.invoke(a,a,b)[0],0)
    def test_generator_only_allowed(self):
        a='0\t2\t30\tlib/CMakeFiles/update_version\thash\n';b=a.replace('\t30\t','\t40\t')
        self.assertEqual(self.invoke(a,a,b)[0],0)
if __name__=='__main__':unittest.main()
