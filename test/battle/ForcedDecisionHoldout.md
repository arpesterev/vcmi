# Production-builder / independent server holdouts (unexecuted stage)

Separate suite `IndependentServer/ForcedDecisionHoldout.*` contains 19 cases.
The existing 58-case oracle matrix, its support gate, and the separate three
DEFEND controls are unchanged. DEFEND must pass before any activation decision.

Four families are fixed before execution, each run at server seeds 29009, 29017,
29021: native MiniTest hero with 20 archers + 20 pikemen versus 47 walking dead;
20 grand elves versus 70 walking dead; 20 grand elves versus 80 walking dead;
40 sprites versus 40 skeletons. Native hero stats/specialty/magic remain intact.
The original neutral 17041 case is also retained explicitly as a known regression,
not called a new holdout. No outcome or model agreement selects cases.

Actual public-map monster creation and onHeroVisit perform server stack splitting,
encounter preparation, initiative, damage and morale. The real fixed legal action
policy is unchanged and the fight runs regardless of capture support, deadline,
recommendation or outcome. No synthetic winning formula or adventure veto acts as
an oracle. Every completed real result logs casualties and initial/lost AI value.

The fixture uses real player-bound CCallback and captureNeutralRisk, then actual
captureNeutralLossPolicy using MiniTest player/owned-army/settings context. A
minimal Nullkiller owns those read-only inputs; this is not a full AI planning
turn, consumer publication, asynchronous scheduler, or CaptureObjects integration.
Capture and exact-key recapture use the real shared state lock; buildDecision
runs outside it on all owned visible terrain/field variants, before the actual
field is selected. No chosen-field hindsight is allowed.

One shared real-clock decision budget begins before capture: 2000 ms, one worker,
384 maximum started scenarios (three possible variants times 128). Public sampled
prefix seeds are 104729 + index*7919; the server seeds are never passed to the
builder. Authoritative RNG is serialized only to assert non-mutation, never to
predict future draws. Recaptured exact source/policy identity must match after
building. Forecast exceptions remain test failures but cannot skip the real fight.

Output keeps capture/policy/record support, all variants, every retained sampled
prefix, status/unsupported reason, recommendation, simulator-policy bounds,
contextual loss tolerance, worst sampled loss, elapsed time and deadline state.
Unknown is not defeat or evidence of safety. Deadline is cooperative, so measured
wall time must be reviewed rather than pretending a hard OS scheduling guarantee.
Full server/model outcomes, including false-favorable and false-dangerous cases,
are retained. This tiny correlated matrix cannot establish calibrated win odds.

Bounds: 19 decisions, at most 7296 started sampled scenarios, 2000 real-action cap
per forced fight. No extra CI job is launched by this staging work. Source needs
the reviewed active-builder/decision-policy/budget overlay before compiling.

## Post-original-case ranged hypothesis expansion

The original 13 indices and every outcome remain mandatory. Six cases appended
at indices 13–18 are 20 grand elves versus ordinary archers 60 and 70, each at
server seeds 29009, 29017, 29021. Both counts are predeclared and retained regardless
of outcome. These are candidates selected after inspecting the original family's
failure, not unseen general calibration or an already demonstrated improvement.

Existing scalar diagnostics establish Catherine attack2/defense2, fighting1.1,
magic1/no spellbook, safe ratio1.1. Baseline own strength is 6620*1.1=7282;
required strengths are 60*126*1.1=8316 and 70*126*1.1=9702. Both are rejected.
The new tests assert that actual production predicate remains false, but still
force the real battle even if the precondition fails. A faster native double-shot
ranged army against slower low-HP shooters provides a mechanical hypothesis, not
a guaranteed win. Real neutral splitting, ordinary ranged penalties and all three
compatible fields remain authoritative. No army/stat/threshold/budget alteration.
