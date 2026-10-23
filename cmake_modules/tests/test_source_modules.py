#!/usr/bin/env python3
"""Dependency-free structural/incremental test of VCMISourceModules.

Usage: python3 cmake_modules/tests/test_source_modules.py --cmake cmake --ninja ninja
Run on Linux or macOS. This is not a replacement for a full VCMI platform build.
"""
import argparse
import pathlib
import subprocess
import tempfile
import time


def run(*args):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise AssertionError(result.stdout)
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    args = parser.parse_args()
    helper = pathlib.Path(__file__).resolve().parents[1] / 'VCMISourceModules.cmake'
    for shared in (False, True):
        for pch in (False, True):
            with tempfile.TemporaryDirectory(prefix='vcmi-modules-') as temporary:
                root = pathlib.Path(temporary)
                files = {
                    'alpha/feature.cpp': '''#include <dep.h>
#include "local.h"
#if !defined(DEPENDENCY_FLAG) || !defined(SOURCE_FLAG) || !defined(VCMI_DLL)
#error missing_compile_requirement
#endif
#if defined(EXPECT_EXPORT) && !defined(vcmi_EXPORTS)
#error missing_original_export_symbol
#endif
#if defined(EXPECT_AI) && !defined(AI_INTERFACE_FLAG)
#error missing_static_cycle_usage
#endif
int feature() { return VALUE + LOCAL; }
''',
                    'alpha/local.h': '#define LOCAL 0\n',
                    'beta/other.cpp': 'int other() { return 1; }\n',
                    'dep/dep.h': '#define VALUE 7\n',
                    'pch.h': '#include <string>\n',
                    'root.cpp': 'int feature(); int other(); int root() {return feature()+other();}\n',
                    'main.cpp': 'int root(); int main() {return root()==8 ? 0 : 1;}\n',
                    'ai.cpp': 'int ai() {return 1;}\n',
                }
                for name, text in files.items():
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(text)
                cmake = '''cmake_minimum_required(VERSION 3.16)
project(source_module_contract LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
include("@HELPER@")
add_library(imported INTERFACE IMPORTED)
set_target_properties(imported PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/dep" INTERFACE_COMPILE_DEFINITIONS DEPENDENCY_FLAG)
add_library(vcmi @KIND@ root.cpp alpha/feature.cpp beta/other.cpp)
target_link_libraries(vcmi PUBLIC imported)
target_compile_definitions(vcmi PRIVATE VCMI_DLL=1 @EXPECT@)
target_include_directories(vcmi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/alpha)
set_source_files_properties(alpha/feature.cpp PROPERTIES COMPILE_DEFINITIONS SOURCE_FLAG)
@PCH@
@AI@
vcmi_add_source_module(vcmi alpha "^alpha/")
vcmi_add_source_module(vcmi beta "^beta/")
vcmi_add_source_module(vcmi absent "^absent/")
if(TARGET vcmi_absent)
  message(FATAL_ERROR "Empty/minimal source group produced a target")
endif()
get_target_property(modules vcmi VCMI_SOURCE_MODULES)
list(LENGTH modules count)
if(NOT count EQUAL 2)
  message(FATAL_ERROR "Unexpected source module count")
endif()
add_executable(check main.cpp)
target_link_libraries(check PRIVATE vcmi)
'''.replace('@HELPER@', helper.as_posix()).replace('@KIND@', 'SHARED' if shared else 'STATIC').replace('@EXPECT@', 'EXPECT_EXPORT' if shared else 'EXPECT_AI').replace('@PCH@', 'target_precompile_headers(vcmi PRIVATE pch.h)' if pch else '').replace('@AI@', '' if shared else '''add_library(BattleAI STATIC ai.cpp)
target_link_libraries(BattleAI PUBLIC vcmi)
target_compile_definitions(BattleAI INTERFACE AI_INTERFACE_FLAG)
target_link_libraries(vcmi PRIVATE BattleAI)''')
                (root / 'CMakeLists.txt').write_text(cmake)
                build = root / 'build'
                run(args.cmake, '-S', str(root), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Debug', f'-DCMAKE_MAKE_PROGRAM={args.ninja}')
                run(args.cmake, '--build', str(build))
                run(str(build / 'check'))
                assert 'no work to do' in run(args.cmake, '--build', str(build))
                objects = {p: p.stat().st_mtime_ns for p in build.rglob('*.o')}
                assert len([p for p in objects if p.name == 'feature.cpp.o']) == 1
                for edit in ('alpha/feature.cpp', 'alpha/local.h'):
                    time.sleep(0.02)
                    with (root / edit).open('a') as stream:
                        stream.write('\n// incremental contract edit\n')
                    run(args.cmake, '--build', str(build))
                    changed = [p for p, timestamp in objects.items() if p.stat().st_mtime_ns != timestamp]
                    assert len(changed) == 1 and changed[0].name == 'feature.cpp.o', changed
                    objects = {p: p.stat().st_mtime_ns for p in objects}
                    run(str(build / 'check'))
                print(f'PASS {"shared" if shared else "static-cycle"}, PCH={pch}: properties, empty module, unique source, no-op, source/header incremental')


if __name__ == '__main__':
    main()
