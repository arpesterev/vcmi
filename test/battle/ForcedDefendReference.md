# Scripted DEFEND mechanics controls (staged, not executed)

This overlay adds three `ForcedDefendReference.*` tests to the existing reference
binary, leaving the frozen 58-case matrix and its four required smoke controls
unchanged. Run the mechanics suite separately; its observations are not terminal
battle outcomes and must not be counted by the 58-case evidence parser.

Each test uses real MiniTest terrain and server battle setup with 20 native wood
elves against 1000 defenders. The fixed script defends both sides, crosses a round
boundary, makes one actual legal ranged attack against the slower defending
stack, and observes removal at the defender's own next activation. Repeat three
cycles, without reseeding. Any real extra good-morale shooter turn is consumed
with a real DEFEND action. No hidden stats or removed penalties.

- Goblins: assert native effective defense below five and test +1 fallback.
- Pikemen: assert effective defense at least five, test 20 percent and positive
  native morale. Both natural cases require positive defender morale.
- Explicit defensive-stance mechanics control: synthetic army has exactly one
  declared permanent DEFENSIVE_STANCE +3 bonus. This is not a natural encounter,
  admission-support test, or evidence of general combat accuracy.

An owned DetachedDefendBonuses wrapper and detached CUnitState are compared with
live server defense and engine-calculated damage ranges before/during/after
DEFEND. The actual shot's health loss must be in the live predicted damage range.
The defending tag must survive the round change, expire at own activation, and
not accumulate on repetition. Full authoritative GameRandomizer serialization
must remain unchanged through detached construction/comparisons and through
defend-only transitions. This is a stronger no-good-morale-draw check than merely
observing no morale animation. The actual shot may legitimately advance RNG.

Tests intentionally stop after the mechanics observations, not after victory.
The adapter itself is exercised against the server, not the sampler action/turn
selection wiring. No sampler win-rate, neutral false-safe resolution, or executed
pass is claimed.
The existing upgraded-neutral SPELL_AFTER_ATTACK unknown remains unchanged.
Bound: three tests, nine actual shots, at most 30 scripted actions total.
