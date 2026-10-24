# Controlled defence reaction after productive economy

Status: replacement case passed natively in qualification8 run 40118626275.
Qualification9 promotes it to the normative interaction; the original strict day3
mine-recovery case and its failure remain preserved in frozen qualification8. No production change,
map edit, army bonus, fog grant or runtime-budget expansion is made here.

Native qualification7 established that the threatened town was actually empty at
the end of red day2. Hero0 had 24pikemen and 9 archers, raw army 3054, zero movement,
HERO_CHAIN lock 3 and an unblocked current-army return path at turn 1 with strength 3359.
Enemy arrival was turn 1 with strength 2640. That is an opportunity, not demonstrated
coverage. Per-turn resetState clears HERO_CHAIN locks; the test does not clear
locks or teleport the hero itself.

The additional case CoveredEconomyThenActualDefenceOnEnemyApproach keeps the same
map and history through the first withdrawal. It requires urgent actual spending
first and conserved resources/stock, then productive mine capture on day2 with
stationed strength or an actual timely current-army return opportunity. Required
strength is the enemy's actual total strength times the existing AI safe ratio.

Blue then legally takes three north, four east, one northeast and one north
step around the ore-mine blocking footprints, ending at visitable
(49,20), visible from an existing ordinary ore observer. Every position/movement
debit is checked. This approach was declared before running the case: nominal
blue cost 941, enemy-to-town 1546, and red direct attack 1792 versus prior observed 1560 movement.
The earlier unexecuted (49,15) draft was corrected because CPathfinder.cpp687–697
allows a discounted final diagonal, making its nominal 1587-cost attack reachable.
The shortest five-diagonal approach would cross the ore-mine footprint, so the
script instead uses the explicitly listed nine-step clear route. The enemy return
route can use five northwest diagonals to (44,15), then the already visible
corridor and final diagonal to town. These nominal calculations are NOT acceptance evidence. On red day3, actual
production paths must show immediate enemy access to town, sufficient own return
this turn, and no immediate own counterattack. If these geometry premises fail,
the case is invalid/incomplete; it must not be tuned merely to fit a result.

After the actual red planner runs, sufficient force must really be stationed in
the town: the garrison hero, plain garrison, or visiting hero individually. Remote
heroes and unperformed garrison merges do not count. All towns, the captured mine,
and the hero must survive. Unsupported battles remain explicit failures, not wins.
This validates a controlled defensive response, not optimal play, a full battle,
or a replay showing what the original enemy would have done.

Run this as the normative isolated subprocess under the
existing 180-second per-case/600-second aggregate interaction budget. Use the same
single generated defence_economy_three_turns.vmap and provenance; no second map is
needed. The historical day3-only assumption remains a documented failure in frozen
qualification8; see AdventureInteractionScenarios.md for exact commit, artifact
ID, expiration and evidence summary.
