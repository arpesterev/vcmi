#!/usr/bin/env bash
# Only Ninja chooses affected objects: never infer C++ dependencies from git paths.
set -euo pipefail
profile="$1"
mkdir -p incremental-evidence
build="out/build/$profile"
export CCACHE_DIR="$HOME/.cache/vcmi-incremental/$profile"
export CCACHE_BASEDIR="$GITHUB_WORKSPACE"
ccache --set-config=compiler_check=content
ccache --set-config=sloppiness=
ccache --max-size=768M
ccache --zero-stats
ccache --show-config > incremental-evidence/ccache-config.txt
start=$(date +%s)
if [[ "$profile" == linux-gcc-test ]]; then
  cmake --preset "$profile" -DCMAKE_BUILD_TYPE=Release -DENABLE_BATTLE_AI=ON -DENABLE_CCACHE=ON -DENABLE_PCH=OFF -DENABLE_NULLKILLER2_AI=ON -DENABLE_STATIC_LIBS=ON -DVCMI_BATTLE_AI_CALLER_PROBES=ON -DVCMI_BATTLE_AI_TEST_TRACE=ON -DVCMI_BATTLE_AI_BENCHMARK_ONLY=OFF -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  targets=(vcmitest)
else
  cmake --preset "$profile" -DENABLE_CCACHE=ON -DENABLE_PCH=OFF -DENABLE_TEST=OFF -DENABLE_NULLKILLER2_AI=ON -DENABLE_BATTLE_AI=ON -DENABLE_GOLDMASTER=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DPACKAGE_NAME_SUFFIX='AI candidate' -DPACKAGE_FILE_NAME='vcmi-1.7.5-ai-modular-arm64'
  targets=(all)
fi
printf '%s\n' "$(( $(date +%s) - start ))" > incremental-evidence/configure-seconds.txt
# Do not accept an enabled option with a missing or bypassed compiler launcher.
grep -q '^ENABLE_CCACHE:BOOL=ON$' "$build/CMakeCache.txt"
python3 CI/incremental_launcher.py --build "$build" --evidence incremental-evidence "${targets[@]}"
cp "$build/.ninja_log" incremental-evidence/ninja-before.log 2>/dev/null || :
start=$(date +%s)
cmake --build --preset "$profile" --target "${targets[@]}" --parallel 2 -- -d explain 2>&1 | tee incremental-evidence/build.log
printf '%s\n' "$(( $(date +%s) - start ))" > incremental-evidence/build-seconds.txt
cp "$build/.ninja_log" incremental-evidence/ninja-after.log
ccache --show-stats > incremental-evidence/ccache-after.txt
ccache --print-stats > incremental-evidence/ccache-after.tsv
# A second build must execute no compilation/archive/link edge. Version metadata
# generators may run, but content-stable output must not invalidate the engine.
cmake --build --preset "$profile" --target "${targets[@]}" --parallel 2 -- -d explain 2>&1 | tee incremental-evidence/noop.log
cp "$build/.ninja_log" incremental-evidence/ninja-noop.log
python3 CI/incremental_report.py --build "$build" --evidence incremental-evidence
