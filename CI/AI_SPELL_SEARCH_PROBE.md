# Bounded real-spell probe contract

Status: source-only, not compiled or executed locally. Execution belongs to the integration workflow. This is a new probe and does not inherit prior test PASS results.

## Delta, not a replacement overlay

Add `test/battleAI/BattleSpellSearchBenchmark.cpp`. Add this one file to `target_sources(vcmitest PRIVATE ...)` in `test/battleAI/BattleCallerProbes.cmake`. Preserve the integrator's other CMake changes and EOF normalization. No production changes.

Build a separate Release target with `ENABLE_BATTLE_AI=ON`, `VCMI_BATTLE_AI_CALLER_PROBES=ON`, `VCMI_BATTLE_AI_TEST_TRACE=OFF`. The baseline-compatible benchmark-only switch may exclude correctness fixtures, but must include this probe. The same fixture is compiled against pinned upstream 1.7.5 and the entire candidate production bundle, not just the estimator. Only public APIs available in upstream 1.7.5 are used.

Run with an external 360-second timeout and pipefail, separately for each revision:

```sh
timeout 360s ./vcmitest --gtest_also_run_disabled_tests \
  --gtest_filter=BattleSpellSearchBenchmark.DISABLED_BoundedRealSpellSearch \
  --gtest_output=xml:spell-search.xml >spell-search.log 2>&1
python3 validate_spell_search.py --revision candidate \
  --log spell-search.log --xml spell-search.xml --output spell-search.json
```

Use `--revision baseline` for upstream. A nonzero executable/timeout exit is a gate failure even if some rows or XML exist. Save log, XML, JSON, revision SHA and build configuration. No flaky timing assertions occur in gtest. The parser rejects caps/incomplete/skipped runs; caps are operational safety limits, not acceptable gameplay latency claims.

## Scenario and evidence

Nine stable cells: 6, 14, 28 live mixed melee/ranged stacks, each with real Magic Arrow, Teleport, and Remove Obstacle. The public `SpellTargetEvaluator::getViableTargets` enumerates real spell mechanics. Every returned target is actually cast with `BattleCast::castEval` into a private hypothetical battle, followed by cache rebuild, `PotentialTargets`, `findMoveTowardsUnreachable`, and `findBestTarget`. The fixture checks finite scoring and unchanged source snapshots/obstacles.

The caster is a real hypothetical unit state, mode PASSIVE, expert spell level, effect power 10 and duration 3. This deliberately avoids hero mana/send callbacks. Three enemy force-field spell obstacles are supplied by the underlying callback, so the fixture also works against upstream's unimplemented hypothetical addObstacle. Units are production detached state objects, not real CStack objects. This is real mechanics plus post-cast search, not a complete hero-turn/game simulation.

Source declarations: source_units = N; friendly_units = N/2; obstacle_entries = 3; hex_domain = 187. Pre-validation Cartesian bounds are N for scalar Magic Arrow, N×187 for Teleport, and 187 for Remove Obstacle. Actual legal target counts are runtime outputs, not assumed counts. Candidate must evaluate every legal target and report at least one changed state in every cell. Baseline must exercise Magic Arrow; its unsupported Teleport and obstacle enumeration return zero targets, which is expected but must never be called a speedup.

One pass per cell, with no percentile claims. Enumeration and evaluation time are separate. Full-bundle ABBA remains a separate benchmark. Repeat this probe externally if distributions are wanted.

## TSV schema v1

Exactly one configuration line:

```text
SPELL_SEARCH_CONFIG\ttrace=off\tmode=passive\tlevel=expert\trounds=1\tcell_cap_ms=60000\ttotal_cap_ms=300000\ttarget_cap=8192\tpasses=1
```

Exactly nine rows, 16 tab-separated fields:

1. `SPELL_SEARCH`
2. stack count (6/14/28)
3. key (`magicArrow`/`teleport`/`removeObstacle`)
4. legal target count
5. evaluated target count
6. status (`COMPLETE`, `TIME_CAP`, `TARGET_CAP`)
7. enumeration milliseconds
8. post-cast evaluation milliseconds
9. finite score checksum
10. number of casts that changed unit health/position or obstacle count
11. mechanics target-signature arity (1/2/1)
12. `obstacle_entries=3`
13. `source_units=N`
14. `friendly_units=N/2`
15. `hex_domain=187`
16. `raw_combination_bound=B`

A total cap additionally emits `SPELL_SEARCH_TOTAL_CAP\t300000`; it is a failure. Cell 60 s, total 300 s and target 8192 limits cannot interrupt a single in-progress engine call; the external timeout bounds that case. Exceeding a cap at enumeration or after the final evaluation also reports TIME_CAP. Skipped trace builds, failed assertions, nonfinite values, missing/duplicate rows or error/debug/mock diagnostics cannot count as success.

The companion Python parser emits schema `vcmi-spell-search-v1`, revision, nine stable IDs `N/key`, per-cell counters/times/checksum/source declarations and `baseline_unsupported`. It is source-reviewed only until workflow execution. Candidate incomplete/capped results block shipping the expanded search; they do not silently truncate production spell choices.
