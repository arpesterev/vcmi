# Bounded BattleAI latency smoke, version 2

This is a resource-bounded descriptive comparison, not a tail-latency estimate,
strength test, full-game benchmark, or automatic performance acceptance.

Each of the four invocations (baseline, candidate, candidate, baseline) uses the
same 24 scenario cells: 6/14/28 stacks, mixed armies off/on, horizon 1/2, and
exchange/finder. Each cell has exactly 3 discarded warmups and 10 measured calls.
The 15-second cell budget and 600-second process budget remain unchanged. The
cell budget includes fixture setup, cache population, target construction,
state checks and warmups, not just the reported operation. Any capped, missing,
skipped or failed cell invalidates the run; no partial result is called complete.

The full changed runtime BattleAI file set is restored to pinned upstream 1.7.5
for the baseline. Both revisions use the identical public-API-compatible harness,
release flags, no trace, CPU affinity and logger settings. Each sample still
checks finite scores, input-state purity and a deterministic action signature.
Across the two invocations of one revision, signatures and candidate counts must
match. Different signatures between revisions are allowed and do not establish
that one decision is stronger.

The report contains sample medians, standard deviations and observed maxima.
An observed maximum from 10 samples is not p95, a tail estimate or a worst-case
bound. Comparisons average the two invocation medians, and separately average
the two observed maxima. Ratios are descriptive; no speed or quality threshold
is silently imposed or passed. A representative gameplay evaluation remains
separate work.

## Why the sampling contract changed

Run 39965486525 retained correct results but could not fit 5 warmups plus 30
measurements into 15 seconds for three 28-stack candidate finder cells. Measured
finder calls took roughly 0.45–0.78 seconds. This establishes a sampling-budget
conflict and an overhead requiring analysis; it is not evidence of a frozen
turn. Version 2 deliberately reduces statistical scope rather than raising the
budget, calling partial data complete, or claiming p95 from fewer samples.
Older failed measurements remain evidence of the old contract, not passing
version-2 measurements.

## Runtime log volume and its negative control

CI passes --gmock_verbose=error to test executables. The test entry point calls
InitGoogleMock so this option is actually parsed; GoogleMock initializes
GoogleTest internally. This suppresses only routine uninteresting-mock warnings,
not unexpected-call failures, assertions, compiler warnings, or production
logging. The default mock behavior is unchanged.

Before the normal suite, a separate opt-in disabled test deliberately calls a
mock with an unexpected argument. CI requires exit status 1 and exactly one XML
failure containing `Unexpected mock function call`. A timeout, skip, missing
case, different failure or successful exit fails the contract. The normal enabled
suite does not execute this intentional failure. Existing full-suite, required
regression, mutant, trace-census and real-spell-search gates remain mandatory.
