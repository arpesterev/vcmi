# Bounded real-spell probe contract

Status of this delta: C++ source-only, not compiled or executed locally. Python parser tests use synthetic log/XML artifacts only and are not engine runs. Actual baseline/candidate execution belongs to the integration workflow; this delta inherits no prior runtime PASS.

## Scope and baseline compatibility

Update `test/battleAI/BattleSpellSearchBenchmark.cpp`, `CI/validate_spell_search.py`, and this contract. `CI/test_validate_spell_search.py` tests the artifact validator without running VCMI. Keep the existing `BattleCallerProbes.cmake` registration and the integrator's other CMake changes. This harness introduces no production API and must remain identical in both builds.

Build a separate Release target with `ENABLE_BATTLE_AI=ON`, `VCMI_BATTLE_AI_CALLER_PROBES=ON`, `VCMI_BATTLE_AI_TEST_TRACE=OFF`. The baseline-compatible benchmark-only switch may exclude correctness fixtures, but must include this probe. Compile the same fixture against pinned upstream 1.7.5 BattleAI and the entire candidate production bundle, not just the estimator. Only public APIs present in upstream 1.7.5 are used, including `Mechanics::canBeCastAt` and `SpellTargetEvaluator::getViableTargets`.

Run with an external 360-second timeout and pipefail, separately for each revision:

```sh
timeout 360s ./vcmitest --gtest_also_run_disabled_tests \
  --gtest_filter=BattleSpellSearchBenchmark.DISABLED_BoundedRealSpellSearch \
  --gtest_output=xml:spell-search.xml >spell-search.log 2>&1
python3 CI/validate_spell_search.py --revision candidate \
  --log spell-search.log --xml spell-search.xml --output spell-search.json
```

Use `--revision baseline` for upstream. A nonzero executable/timeout exit is a gate failure even if some rows or XML exist. Save log, XML, JSON, revision SHA and build configuration. No timing assertions occur in gtest. Parser cap rejection is an operational gate, not acceptable gameplay latency or a timing regression test.

## Scenario and evidence

Nine stable cells: 6, 14, 28 live mixed melee/ranged stacks, each with real Magic Arrow, Teleport, and Remove Obstacle. Preserve their current fixture positions, three enemy force-field obstacles, passive caster, expert spell level, effect power 10, effect duration 3, and one post-cast search round. Units are production detached state objects, not real CStack objects. Explicit obstacle callback expectations make the same fixture usable against the upstream baseline. This is real mechanics plus post-cast search, not a complete hero-turn/game simulation.

There are two distinct target sets:

1. The harness independently audits the full declared domain with real `Mechanics::canBeCastAt`, without calling candidate-only helpers. Magic Arrow checks every source-unit position. Teleport checks every complete CREATURE+LOCATION pair from all source-unit positions times all 187 battlefield cells. Remove Obstacle checks all 187 cells. The audit does not prefilter by ownership, accessibility, heuristic score, or no-op status; real mechanics decides legality. It records the count of unique legal target tuples as `legal_pool` and the number of legality checks as `legal_checked`.
2. `SpellTargetEvaluator::getViableTargets` supplies the selected list for expensive cast evaluation. Each selected target must have the expected complete signature, pass real mechanics again, be unique, and belong to the independently audited legal pool. The fixture has distinct single-hex units, so ordered hex tuples are canonical keys; Teleport keys retain both source-unit position and destination. This is a fixture-local representation, not a general wide-unit canonicalization API.

Every selected target, not every legal target, is cast with `BattleCast::castEval` into a private hypothetical battle. Each cast is followed by damage-cache rebuild, `PotentialTargets`, `findMoveTowardsUnreachable`, and `findBestTarget`. Finite-score and unchanged source snapshots/obstacles checks remain. A supported cell must select/evaluate at least one target and produce at least one actual health/position/obstacle change.

Candidate Teleport selection is an explicitly bounded heuristic: at most eight selected legal pairs for the actual Teleport CREATURE+LOCATION signature. The expensive evaluator is exhaustive only over that shortlist. The harness does not assert that all legal Teleports are selected or that the full-search optimum is retained. Legal no-op Teleports can be included in `legal_pool` but omitted from selection; those omissions are included in `pruned`. The production cheap legality scan and the no-spell comparison baseline are not replaced by this audit or probe.

For Magic Arrow and Remove Obstacle, nonempty selection must equal the independently audited legal pool; their `selection_mode` is `full_enumeration` and `pruned=0`. This claim is specific to these two audited spell fixtures, not all location-spell heuristics.

Pinned baseline Magic Arrow must exercise real effects. Its unsupported Teleport and obstacle enumerators return zero selected/evaluated targets despite a nonempty audited legal pool. Such rows use `selection_mode=empty_enumerator`, `pruned=0`, and JSON `baseline_unsupported=true`. Missing baseline enumeration is not 100% pruning, a working shortlist, or a latency speedup. A candidate empty enumerator is rejected.

## Domains and counters

For N source units: `source_units=N`, `friendly_units=N/2`, `obstacle_entries=3`, `hex_domain=187`. `raw_combination_bound` is the pre-validation raw domain, never the legal-pool or shortlist size: N for Magic Arrow, N×187 for Teleport, and 187 for Remove Obstacle. `legal_checked` must equal that raw bound. The full audit must yield `0 < legal_pool <= raw_combination_bound`.

`selected` equals existing TSV field 4 and the list returned by `getViableTargets`; `evaluated` must equal selected on a complete row. `selected_unique` and `selected_in_pool` must both equal selected, with gtest proving actual tuple uniqueness/membership. The parser checks the counters and successful test XML, not the target tuples themselves.

For supported cells, `legal_pool = selected + pruned`. Candidate Teleport additionally requires `1 <= selected <= 8`. For baseline unsupported cells, `selected=evaluated=changed=pruned=0`, checksum is zero, and the nonzero legal pool represents unavailable enumeration instead of pruning.

## Timing and caps

One pass per cell, with no percentile or speedup claims. `legality_audit_ms` is independent harness legality enumeration, not candidate enumeration time. Existing `enumeration_ms` measures only `getViableTargets`, which includes its production legality scan and selection. Existing `evaluation_ms` measures post-cast work for the selected list. `cell_ms` includes the audit, production enumeration, selected-target validation, post-cast evaluation and source nonmutation checks. Total elapsed time additionally includes fixture/cache setup and work between cells. Audit time is therefore never omitted from the 60-second cell or 300-second total cap.

A total cap before a cell emits `SPELL_SEARCH_TOTAL_CAP\t300000`; it is a failure. Cell/total caps are checked after enumeration and after the final evaluation/nonmutation checks as well as between evaluations. The 8192 target cap remains. Individual engine calls and the full cheap audit/production scan are not preemptible, so neither K=8 nor these checks guarantees a hard per-call time bound; the external 360-second timeout is still required.

Skipped trace builds, failed assertions, nonfinite values, missing/duplicate rows or metadata, inconsistent counters, capped/incomplete rows, missing completion footer, or error/debug/mock diagnostics cannot count as success. Full-bundle ABBA and gameplay-quality evaluation remain separate benchmarks. Repeat this probe externally if distributions are wanted.

## TSV schema v2

Exactly one configuration line, before the rows (`\t` denotes a tab):

```text
SPELL_SEARCH_CONFIG\ttrace=off\tmode=passive\tlevel=expert\trounds=1\tcell_cap_ms=60000\ttotal_cap_ms=300000\ttarget_cap=8192\tpasses=1\tschema=2\taudit=complete_domain\tteleport_target_cap=8
```

Exactly nine rows, each with 25 tab-separated fields. Positions 1–16 are preserved, but field 4 is explicitly selected count, not an exhaustive legal count:

1. `SPELL_SEARCH`
2. stack count (6/14/28)
3. key (`magicArrow`/`teleport`/`removeObstacle`)
4. selected target count
5. evaluated target count
6. status (`COMPLETE`, `TIME_CAP`, `TARGET_CAP`)
7. production enumeration milliseconds
8. post-cast evaluation milliseconds
9. finite score checksum
10. number of casts that changed unit health/position or obstacle count
11. mechanics target-signature arity (1/2/1)
12. `obstacle_entries=3`
13. `source_units=N`
14. `friendly_units=N/2`
15. `hex_domain=187`
16. `raw_combination_bound=B`
17. `legal_pool=L`
18. `selected=S`
19. `pruned=P`
20. `selection_mode=full_enumeration|teleport_k8|empty_enumerator`
21. `legal_checked=B`
22. `selected_in_pool=S`
23. `selected_unique=S`
24. `legality_audit_ms=A`
25. `cell_ms=C`

Every named declaration must appear exactly once with no unknown keys. The parser accepts declaration order changes but not missing/duplicate names. Counts are canonical nonnegative decimal integers. All times/checksum are finite; times are nonnegative, cell time is less than 60000 ms, and audit + enumeration + evaluation time must fit within cell time (1e-6 ms rounding allowance).

Exactly one completion footer follows all nine rows:

```text
SPELL_SEARCH_END\tcells=9\ttotal_ms=T\tstatus=COMPLETE
```

The C++ probe reports `status=TIME_CAP` instead if its final total reaches 300000 ms. A successful parser requires COMPLETE and a finite nonnegative total below 300000 ms; the sum of all cell times must fit inside it (same rounding allowance). No partial/old-v1 artifact can pass as schema v2.

The companion parser emits `vcmi-spell-search-v2`, revision, nine stable IDs `N/key`, original per-cell fields, new audit/selection counters, `selection_mode`, `baseline_unsupported`, complete timing, total time, and limits including `teleport_selected=8`. Its note explicitly disclaims full-optimum search and hard gameplay latency guarantees.

## Source-only validator checks

Run the parser contract tests without a VCMI build:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s CI -p test_validate_spell_search.py -v
```

These tests use invented counts/times, cover accepted baseline and shortlisted candidate artifacts, and reject malformed/duplicate/missing metadata, count and membership inconsistencies, a ninth Teleport target, incomplete non-Teleport enumeration, mislabeled unsupported baseline rows, missing/skipped/failed XML, diagnostics, caps and invalid timing. They establish parser behavior only, not successful compilation, actual legal-pool sizes, runtime improvement, battle quality, or the correctness of production ranking.
