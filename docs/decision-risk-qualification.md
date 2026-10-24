# Decision-risk integration qualification candidate

Default activation remains disabled by the gateway. This candidate is not a claim of calibrated real-battle probabilities or improved match win rate.

## Production seams

- `bypassBattle` consults eligible favorable evidence before the legacy scalar loss pruner. It does not veto paths globally: defence/escape may value costly combat.
- Ordinary, non-forced `CaptureObjectsBehavior::getVisitGoals` can reject robust predicted defeat. Forced contexts retain their existing treatment. Favorable evidence supplies estimated casualty value to the existing path scorer.
- Only exact visible ordinary neutral monsters, current own army, no earlier danger/loss/spent mana, and a single destination encounter are eligible. Unknown graph provenance, hero exchanges, earlier combat (even zero estimated casualties), overlapping guards, siege and enemy heroes retain legacy handling.
- Current owned-army/castle loss policy is frozen at capture and recapture, not read from a possibly stale ArmyManager cache. The existing scorer and capture share the same willingness formula. Physics-only memo hits refresh policy and own-magic directionality.

## Boundaries and budget

One shared deadline capability covers all captured candidates, contexts and replans until the next action attempt. The default is 1.5 seconds, hard ceiling 2 seconds and 1792 attempted draws; not two seconds per fight. Maximum seven fairly admitted hero/target pairs, three compatible visible fields per pair, deterministic seed prefixes at 16/32/64/128. More than three fields is unknown; no field is selected using the actual oracle outcome. All variants must qualify. Incomplete prefixes are unknown.

Workers join before return. Same-snapshot access is serialized because engine const APIs can have mutable caches; only independently owned contexts can run concurrently. Deadline/cancellation is checked per activation, and every attempted draw is debited even if cancellation races its start. The budget does not promise an end-to-end two-second game action: other planning work and hardware cost require measurement.

The one-sided binomial calculations describe sampled policy outcomes, not arbitrary engine/BattleAI outcomes. Five percent is an explicit conservative gameplay threshold for favorable rescue. Family allocation covers the fixed checkpoints, all represented fields and both directions for one fixed policy/context. Unknown own magic prevents a no-spell defeat veto. A handful of lucky wins is never sufficient.

## Tests and evidence

Prior qualification: exact DEFEND mechanics controls passed, 58 real fights completed, 168/174 legacy sampled outcomes supported. The ordinary neutral challenge remained a real false-safe winner counterexample even after DEFEND; this is retained for independent adaptive holdouts, not removed from the matrix.

New protocol tests exercise actual CaptureObjects with injected immutable records, source metadata inheritance, incomplete/mixed variants, cost context and magic directionality. Injected records test routing only, not calibration. Separate fixtures exercise real updatePaths/bypassBattle on MiniTest-derived maps and independent forced holdout encounters. These new tests have not yet been compiled/executed at this manifest stage.

Local dependency-free checks passed: 24,642 finite shared-policy equivalence cases plus NaN fallback; three-field zero-adverse 128-draw upper bound 0.047088. Full linked model/scheduler compilation and measured deadline behavior remain qualification gates. User M4 Pro latency is not inferred from CI timings.
