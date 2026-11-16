# Current observed Defend in the live mixed leaf

This extension consumes an existing `CurrentDefendSnapshot` after an observed,
complete authoritative canonical DEFEND action. It does not reconstruct an
unobserved stance, extend snapshot lifetime, certify future effects, or change
source-order history. A single latest witness cannot certify multiple defended
units. No witness argument preserves the previous rejection behavior.

## Source and native mapping

Pinned e12906accf96c2662b1abadbc49ddc111991bf21 command.cpp:2521-2542 sets Done and
Defending, computes `max(defenseSkill*20/100,1)`, and adds it directly to the
current `m_monInfo.defenseSkill`. army.cpp:2095 getAdjustedDefense reads that field;
getDefenseModifier:2123 subtracts the original creature defense trait. The source
new-turn path army.cpp:4111-4114 subtracts the recorded fixed bonus and clears
Defending. The port therefore feeds the witnessed **current defense once** into
source unit combat value, minima, attack/retaliation pairs, threat and target
selection. It does not add another stance bonus to a current effective stat.

Native canonical +1 (underlying defense below5) or 20%-with-zero-extra records
must match the existing session's before/effect/after provenance and current
context. The source DEFEND also checks obstacle attacks. Consequently an injury,
attack, movement, spell, unknown effect or interruption after the stance is not
covered by merely seeing three bonuses: existing session/controller invalidation
remains mandatory. Constructor stance admission checks the same exact snapshot,
not a permissive UNIT_DEFENDING whitelist.

NativeShooterModel is used here only as the existing effective-record validator.
Other callers still omit the optional witness and reject current Defend. This
change does not qualify that model's hypothetical future damage calculations.
All mixed mapper unit/profile/modifier and melee/ranged pairs receive the witness;
self and same-side pairs are not omitted. Unsupported conditional/modded/proc
mechanisms remain unsupported under the existing ordinary input scope.

## History and scope

An unstaged DEFEND still invalidates eligible source-order knowledge. Current
Defend evidence never promotes source rank or producer identity. Allalive live
melee can legitimately rebuild eligible order; shooter selection does not consume
rank. Previously unknown corpse ranks remain unknown and retain their existing
rejection. No enemy-SHOOT provenance widening is included.

The session certifies only the latest uninterrupted completed DEFEND. Earlier
stances, multi-defended rosters, missing/revoked witnesses and round transitions
remain explicit fallback cases. This is a practical immediate next-choice
extension, not complete persistent Defend support.

## Evidence

HoMM3MixedDefendTest.cpp exercises native server DEFEND and SetStackEffect,
post-action witness capture, actual next controller choice and legal execution.
Direct native creature base facts feed the independently namespaced pinned
original doCompAI host; source Defend is applied once to its mutable defense field,
while original creature traits stay unchanged. Both +1 and20% branches compare
source choice and all defended melee-pair values. Far archer genuinely SHOOTs;
near melee executes native attack/retaliation. A fixed four-unit layout provides
16 consecutive source-vs-legacy differentiating actual controller decisions.

Negative tests cover absent/revoked witness and unchanged RNG, multiple defended
units with one latest witness, round expiry, and unpaired injury invalidation.
The accepted predecessor comparison uses a test-only overload shim that discards
the new argument and delegates unchanged a36 implementations; no predecessor
algorithm or controller is edited. Full integrated regression is separate.
