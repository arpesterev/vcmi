import importlib.util,json,pathlib,shutil,subprocess,tempfile,unittest
from unittest import mock
spec=importlib.util.spec_from_file_location('probes',pathlib.Path(__file__).with_name('incremental_probes.py'));probes=importlib.util.module_from_spec(spec);spec.loader.exec_module(probes)

class ProbeTest(unittest.TestCase):
    def test_actual_ninja_graph_and_restore(self):
        cmake=shutil.which('cmake');ninja=shutil.which('ninja')
        if not cmake or not ninja:self.skipTest('cmake/ninja unavailable')
        with tempfile.TemporaryDirectory() as temporary:
            root=pathlib.Path(temporary);build=root/'build';evidence=root/'evidence'
            files={'lib/shared.h':'#pragma once\n#define VALUE 2\n','client/private.h':'#pragma once\n#define PRIVATE_VALUE 3\n','client/main.cpp':'#include "lib/shared.h"\n#include "private.h"\nint clientmain() {return VALUE+PRIVATE_VALUE;}\n','client/extra.cpp':'#include "private.h"\nint extra() {return PRIVATE_VALUE;}\n','lib/core.cpp':'#include "shared.h"\nint core() {return VALUE;}\n','AI/BattleAI/worker.cpp':'#include "lib/shared.h"\nint battle() {return VALUE;}\n','CMakeLists.txt':'''cmake_minimum_required(VERSION 3.16)
project(probe_fixture LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
include_directories(${CMAKE_SOURCE_DIR})
set(generated_sources)
foreach(index RANGE 1 30)
  set(generated "${CMAKE_BINARY_DIR}/moc_generated_${index}.cpp")
  file(WRITE "${generated}" "#include <lib/shared.h>\nint generated_${index}() { return VALUE; }\n")
  list(APPEND generated_sources "${generated}")
endforeach()
add_library(vcmiclientcommon_render STATIC client/main.cpp client/extra.cpp ${generated_sources})
add_library(vcmi_core STATIC lib/core.cpp)
add_library(BattleAI STATIC AI/BattleAI/worker.cpp)
'''}
            for name,text in files.items():p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
            subprocess.check_call([cmake,'-G','Ninja','-S',str(root),'-B',str(build)],stdout=subprocess.DEVNULL)
            subprocess.check_call([ninja,'-C',str(build)],stdout=subprocess.DEVNULL)
            original={p: p.read_bytes() for p in root.rglob('*') if p.is_file() and not probes.within(p,build)}
            objects=probes.commands(build,root)
            generated={obj for obj,path in objects.items() if probes.within(path,build)}
            self.assertEqual(len(generated),30)
            deps=probes.parse_deps(subprocess.check_output([ninja,'-C',str(build),'-t','deps'],text=True),build,objects)
            self.assertEqual(len(deps[root/'lib/shared.h']),33)
            with self.assertRaisesRegex(ValueError,'public shared header'):
                probes.select_probes(root,build,objects,deps,32)
            report=probes.run_probes(root,build,evidence,ninja,60,20,33,2)
            self.assertEqual(report['status'],'passed');self.assertTrue(report['clean_sources']);self.assertEqual(len(report['probes']),4)
            public=next(p for p in report['plan'] if p['probe']=='public-shared-header');self.assertEqual(public['families'],['ai','client','core']);self.assertEqual(len(public['expected_objects']),33);self.assertTrue(generated <= set(public['expected_objects']))
            private=next(p for p in report['plan'] if p['probe']=='private-client-header');self.assertEqual(len(private['expected_objects']),2)
            for path,data in original.items():self.assertEqual(path.read_bytes(),data)
            for item in report['probes']:self.assertEqual(item['expected_objects'],item['actual_objects']);self.assertTrue(item['source_restored'])
            real_execute=probes.execute
            def fail_probe(command,cwd,logfile,seconds):
                if logfile.name=='client-source.log':raise RuntimeError('injected compile failure')
                return real_execute(command,cwd,logfile,seconds)
            with mock.patch.object(probes,'execute',side_effect=fail_probe):
                with self.assertRaisesRegex(RuntimeError,'injected compile failure'):
                    probes.run_probes(root,build,root/'failure-evidence',ninja,60,20,33,2)
            failed=json.loads((root/'failure-evidence/probe-report.json').read_text())
            self.assertEqual(failed['status'],'incomplete');self.assertTrue(failed['clean_sources'])
            for path,data in original.items():self.assertEqual(path.read_bytes(),data)
            before=probes.log_entries(build)
            subprocess.check_call([ninja,'-C',str(build)],stdout=subprocess.DEVNULL)
            self.assertEqual(probes.changed_objects(before,probes.log_entries(build),probes.commands(build,root)),set())

    def test_silent_extra_binary_edges_rejected(self):
        ninja=shutil.which('ninja')
        if not ninja:self.skipTest('ninja unavailable')
        with tempfile.TemporaryDirectory() as temporary:
            build=pathlib.Path(temporary)
            # Descriptions deliberately contain neither "link" nor "archive".
            (build/'build.ninja').write_text("rule CXX_EXECUTABLE_LINKER__fixture\n  command = printf binary > $out\n  description = quiet\nrule CXX_STATIC_LIBRARY_LINKER__fixture\n  command = printf archive > $out\n  description = quiet\nbuild program: CXX_EXECUTABLE_LINKER__fixture\nbuild libfixture.a: CXX_STATIC_LIBRARY_LINKER__fixture\ndefault program libfixture.a\n")
            subprocess.check_call([ninja,'-C',str(build)],stdout=subprocess.DEVNULL)
            binaries=probes.binary_outputs(ninja,build)
            self.assertEqual(binaries,{'program','libfixture.a'})
            for output in sorted(binaries):
                before=probes.log_entries(build)
                (build/output).unlink()
                subprocess.check_call([ninja,'-C',str(build)],stdout=subprocess.DEVNULL)
                after=probes.log_entries(build)
                self.assertEqual(probes.changed_objects(before,after,{}),set())
                with self.assertRaisesRegex(RuntimeError,'object/archive/link edges rebuilt'):
                    probes.require_noop(before,after,{},binaries,'No-op')
            before=probes.log_entries(build)
            subprocess.check_call([ninja,'-C',str(build)],stdout=subprocess.DEVNULL)
            probes.require_noop(before,probes.log_entries(build),{},binaries,'No-op')

    def test_external_compile_commands_retained_but_not_mutated(self):
        with tempfile.TemporaryDirectory() as temporary:
            base=pathlib.Path(temporary);root=base/'checkout';build=root/'build'
            build.mkdir(parents=True)
            external=base/'external.cpp';external.write_text('int external() {return 0;}')
            (build/'compile_commands.json').write_text(json.dumps([
                {'directory':str(build),'file':str(external),
                 'arguments':['c++','-c',str(external),'-o','external.o']}]))
            objects=probes.commands(build,root)
            self.assertEqual(objects,{'external.o':external})
            before={'external.o':['0','1','2','external.o','4']}
            after={'external.o':['0','1','3','external.o','4']}
            with self.assertRaisesRegex(RuntimeError,'external.o'):
                probes.require_noop(before,after,objects,set(),'No-op')
            with self.assertRaisesRegex(ValueError,'No bounded valid candidate'):
                probes.select_probes(root,build,objects,{external:{'external.o'}},32)

    def test_missing_candidates_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=pathlib.Path(temporary)
            with self.assertRaisesRegex(ValueError,'No bounded valid candidate'):probes.select_probes(root,root/'build',{}, {},32)
    def test_invalid_deps_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=pathlib.Path(temporary)
            with self.assertRaisesRegex(ValueError,'Invalid Ninja'):probes.parse_deps('a.o: #deps 2, deps mtime 1 (STALE)\n',root,{'a.o':root/'a.cpp'})
    def test_object_escape_rejected(self):
        with self.assertRaises(ValueError):probes.output_name('../outside.o',pathlib.Path('/tmp/build'))
if __name__=='__main__':unittest.main(verbosity=2)
