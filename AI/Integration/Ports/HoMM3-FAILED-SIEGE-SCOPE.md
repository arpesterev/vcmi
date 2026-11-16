# HoMM3 failed-siege live withdrawal

Pinned source: sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21,
ai.cpp::failedSiege and aiCheckRetreat; command.cpp::checkGetAIMove.

The new live consumer implements the early failed-siege branch before artifact
valuation and combat simulation. checkGetAIMove consumes aiCheckRetreat before
doCompAI and sets retreat action 4 for a nonhuman side. This is not just the
separate retreating hint supplied to the source spellcaster.

Admission is deliberately narrower than the source:
- computer-controlled attacker only; human and three-argument preference/autocombat
  initialization cannot withdraw automatically; authoritative player ownership
  also must identify a computer player;
- Hard, Expert or Impossible difficulty; Normal's random gate is not guessed;
- an owned tavern exists, and the map has only simple standard victory objectives;
- VCMI currently permits the hero to flee, and our hero does not wear Shackles
  of War (native legality alone ignores their negative permission without an
  opposing hero, unlike the explicit source artifact gate);
- a fortified town has a closed gate and all four walkable wall segments survive;
- living attackers are grounded non-shooters outside the walls, with no living
  catapult; all living non-turret defenders are inside;
- cloned, summoned, ownership-changed units and unavailable geometry decline.

Native wall geometry, battle legality and the actual retreat command remain
authoritative. Unknown/custom victory expressions decline rather than risking a
protected campaign hero. The four source wall targets map to VCMI's four
destructible walkable wall segments, not towers or the keep.

This is not full withdrawal AI. Surrender valuation, valuable-artifact/experience
thresholds, full defeat simulation, Normal-difficulty RNG, defender escape
tunnels, blocked/depleted archers and human confirmation dialogs remain outside
this path. Declined cases continue through the existing controller.

Native tests cover factory-controller and server acceptance of retreat, the
actual three-argument preference initialization safeguard, and each major
negative gate.
