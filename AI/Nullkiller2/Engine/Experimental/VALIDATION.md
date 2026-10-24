# Planning-risk staging status

Not production-wired. No combat strength, win-rate, calibration or performance claim has been established.

## Implemented, awaiting compile and reference execution

- Immutable copied army/context construction. Settings and date are captured; no live callback is retained. Hypothetical constructors are explicit. Own hero creature-type/hero-level specialties may be frozen only after engine evaluation, for the supported no-transform/no-level-change policy; other dynamic/propagated effects remain unknown.
- Separate pure battlefield candidate APIs. The original authoritative battlefield-selection function is preserved, including its legacy RNG stream and draw behavior. The new object query is nonvirtual/static and recognizes exact audited native types; unknown subclasses return unknown without expanding existing object vtables.
- Player-visible ordinary-neutral capture and exact current-own-hero identity checks. Hidden enemy heroes, towns, banks and unknown context are not modeled.
- Ordinary neutral encounter splitting uses `CGCreature::getNumberOfStacks`. A deterministic singleton same-faction upgrade replaces intrinsic bonuses rather than adding them. Changed undead status, stack XP and named stacking alternatives are unsupported.
- Engine-owned single/double-wide placement and reachability, at-most-two-attack, up-to-seven-stack-per-side fixed-policy rollout. Additional/unlimited/no-retaliation and retaliation blocking use engine counters and first-strike sequence rules. Engine damage sampling, health, ammunition, retaliation, reachability, initiative, morale and luck. Recompute next actor after every activation.
- Independent seeded `GameRandomizer`; fresh morale/luck bias history is a scenario assumption, not a copy of the authoritative future or calibrated probability.
- Ordinary static obstacles only. Siege, dynamic obstacles, complex unit effects and required DEFEND actions return unknown. Affordable own magic marks directional use: no-spell defeat/high losses must not veto a plausible magic-assisted fight.
- Fixed limits: 256 actions and 64 rounds per scenario; at most seven table entries, two compatible battlefield variants and three samples per variant. A same-turn immutable memo carries a 42-scenario total budget, at most 10,752 rollout actions per turn. Exact physics reuse costs no new rollout; source/epoch stamps remain separately revalidated. The scheduler must serialize builds and retain spent-budget memo even when publication fails; parallel or stale-memo callers are not globally budgeted by the container. Wall microseconds recorded, not yet measured.
- Immutable table, bounded turn-scoped physics memo and exact source descriptors. Cheap round-robin hero admission precedes capture. Duplicate captures are rejected. Lookup rejects changed epoch/state, transferred armies, earlier aggregate casualties and spent route mana.

## Required before production use

1. Compile the staging files and register the source invariance/table tests.
2. Run server-completed reference fights independently of adventure acceptance. Include ordinary morale, mixed equal-speed stacks with a killed pending actor, direct neutral encounter split and singleton upgrade, static obstacles and a blocked DEFEND case.
3. Assert source army/hero/creature versions, parent/child links, exported bonuses, health, mana and both RNG states unchanged through capture, simulation and destruction.
4. Report coverage, unsupported reasons, favorable/unsafe winners, losses and disagreement against real outcomes. Container expectations are not combat calibration.
5. Add gateway-owned scheduler using its actual held shared lock. Bound eligible pairs BEFORE capture, release the game lock only before simulation and before acquiring the AI-state lock, then reacquire, recapture eligibility/source descriptors and publish only matching records.
6. Integrate before path rejection as well as final goal selection, with actual producer tests proving both favorable rescue and unsafe rejection. Keep unknown states on existing policy rather than inventing penalties.

The table container alone does not establish a safe scheduler or complete invalidation. No production risk decision formula is selected yet.
