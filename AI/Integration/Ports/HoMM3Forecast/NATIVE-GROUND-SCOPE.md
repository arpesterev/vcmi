# Native ordinary live melee projection

`captureNativeOrdinaryGround` captures current public effective facts on the battle thread. `chooseNativeOrdinaryLiveMelee` runs the pinned ordinary live chooser for the actual active unit, then validates and constructs a native command. It does not consume the first forecast action or a simulated expected-damage ledger.

Initial admitted domain:

- Verified own activation/lifecycle and uninterrupted public static-map certificate.
- All retained units alive, ordinary single-hex walking melee, no shooters, clones, summons, machines, current defend stance, creature casting, or unsupported effective bonuses.
- No fighting hero on the opposing side; our own hero must have no spellbook and no native casting permission. See OWN-HERO-GROUND-SCOPE.md for the source zero-pressure proof. Mana-zero heroes are not treated as absent.
- Source speed/attack/defense/damage/value facts fit their checked scalar domains. Source movement distances agree with native public reachability across the unoccupied lattice.
- Source move-order ranks must be invariant. Equal-key paired-index groups use the reviewed invariant fast path; other equal-key blocks enumerate all comparator-valid orientations within a 4096-orientation budget. Admission requires the same complete ranks for every orientation. Common mirrored armies can therefore be supported, while non-invariant or unproved-budget cases remain unavailable. Live supporting-melee census still uses these exact ranks.

All ordered melee pairs are captured, including self and same-side pairs. `canRetaliate[{defender, attacker}]` describes the defender's remaining retaliation, independently of current side. Difficulty/human policy and current side remain distinct source inputs.

The consumer checks actual active identity/control, native MOVE reach, MELEE target and legal attack-from hex, and WAIT eligibility. It commits only the source-consumed prefix of its private random stream after successful capture and native legal validation. Rejection leaves that stream unchanged and the controller uses its existing fallback. Source factors are 75–100 inclusive; this does not claim retail executable RNG identity.

This is a source choice over validated **current public effective inputs**, followed by a legal native command. It is not a universal no-proc certificate, an exhaustive raw bonus-graph/latent limiter audit, or proof that future native damage/outcomes reproduce the retail engine. Stock ordinary fixtures are verified. Unsupported effective records reject; inactive custom proc/limiter provenance remains a stated limitation shared with the existing native shooter projection.

`HoMM3GroundNativeTest.cpp` exercises full ordered-pair mapping, asymmetric spent retaliation, explicit rejection boundaries and RNG rollback. Twelve native fixtures (one to three troops per side, near/far, both active sides) match the independent pinned original live chooser's actor/order/target/value/danger/marks/draw count. Eight choose an actual active different from the forecast's first unit. Every fixture's native consumer command is accepted by the server. These checks do not cover launcher/macOS/GUI play or complete AI acceptance.
