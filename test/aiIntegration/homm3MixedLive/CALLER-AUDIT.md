# Read-only caller, scratch, facing and dead-row audit

Pinned upstream: sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21.
Source files: ai.cpp, ai_tactical.cpp, army.cpp, cmbtmgr.cpp, findpath.cpp in ../../vcmi_a29/evidence/ground_forecast/upstream (workspace-relative vcmi_a29/evidence/ground_forecast/upstream); command.cpp and other newly downloaded runtime files in evidence/upstream. No production/test edits in this audit.

## Actual live caller exists

command.cpp combatManager::main (256; process_action around358) checks m_nextAction==0 then calls checkGetAIMove for computer actions. checkGetAIMove (2062) calls aiCheckRetreat before doCompAI(m_currentSide) (2127), unless placement; retreat/surrender can terminate dispatch. getControl (2134) resets nextAction and calls doSpellAI (2258). doSpellAI (ai.cpp2353) may create type_AI_spellcaster, whose constructor (ai_tactical.cpp706 onward) runs findMoveOrder, simulateCombat(side,0), then findAITargets for both sides. aiCheckRetreat (ai.cpp278) can also simulate both prefixes.

No missing reconstructed ordinary caller gap: it is in command.cpp, not cmbtmgr.cpp. This does NOT establish complete hero spell/retreat integration in the port.

## Scratch survives but relevant live census is rebuilt

simulateCombat (ai.cpp2240) saves/restores only nextAction, Extra, GridIndex, GridIndex2. It clears expected damage for ordered actors before simulation; resulting expected damage and target scratch survive. doCompAI (1997) resets the actual actor's groupToAttack/indexToAttack, not target census or expected damage.

Live chooseMeleeAction (1907) constructs non-simulated data, recomputes move order, then findAITargets(side,currentArmy,1). Every own row is clearAIValues() before any skip (2268 onward). getAttackChange (653) consults exclusively this same m_currentSide roster's target pointer/value/possible-target bits. Live chooseShooterAction (526) calls findAITargets(1-side,0,0), rebuilding the entire enemy census consumed by getRangedAttackValue (ai_tactical.cpp239). m_simulated=false means getTotalHitPoints (army.cpp2384) and getSimpleAttackEffect (ai_tactical.cpp195) ignore retained expected damage. Thus stale predecessor scratch does not require replaying retreat/hero forecast to reproduce these ordinary live leaves, PROVIDED the relevant census is rebuilt exactly, including all skipped-row clears, side/current-side consistency and native source-state facts. A cleared scratch oracle is not proof by itself; add poisoned predecessor scratch parity tests.

## Facing is restored, not globally reset

army initialization (army.cpp205) sets facing=1-newGroup. Ordinary movement: command.cpp2442 calls moveTo(...,1); moveTo3528 forwards to simpleMove; walkTo1944 saves and2008 restores prior facing. Shoot: command2458 attackHex(...,1), rangeAttack1287 saves and1305 restores. Move-then-attack: command2290 saves prior facing, move/attack use restore=false, then2317 restores unless immobilized. Defender doAttack1858 saves and1893 restores prior facing unless immobilized. Creature cast4615 saves and4704 restores (normal rendered combat).

Therefore fresh ordinary ground/non-immobilized stacks retain initial 1-group across these supported actions. There is no general doCompAI-boundary reset. Arbitrary restored snapshots, earlier unsupported transformations/control changes/death-resurrection/special actions are not proven. Native inference needs a maintained supported-history/fresh-battle certificate or an explicitly documented orientation adaptation, not an unqualified current-side assumption. chooseDefenseHex (ai.cpp1130) genuinely uses the shooter's facing for equal-travel/equal-coverage X tie choice.

## Dead rows

army::damage2780 reduces count to zero and sets m_allUnitsKilled. processDeath2818 sets creatureImmobilized (2836), clears occupied-cell side/slot, preserves roster index and does not shrink m_numArmies. cmbtmgr.cpp3424 onward processes killed rows at end of powEffect. findAITargets clears then skips immobilized own rows; its enemy loop does NOT explicitly skip immobilized/dead rows in live mode. Melee path-valid-cell filtering and live zero HP affect scoring; shooter can still enumerate dead rows. Dropping dead roster entries or compacting indices is not source-equivalent without proof. Death handling should preserve original slot identity and verify zero-valued candidates/tie order separately before broadening native acceptance.

## Other exact implementation traps

chooseDefenseHex counts all unoccupied/mover-occupied neighboring cells in openHexes, not only reachable cells; uses six directions for single hex; later equal X wins. attemptShooterDefense tests final loop-local hex against current actor hex (not saved bestHex) before deciding DEFEND, then moves toward bestHex. Preserve source behavior unless adaptation is explicit. getTravelTime (findpath.cpp1335) divides by speed, so speed-zero support needs a caller-path proof, not guessed arithmetic.
