# FHeroes2 strength differential oracle

Pinned ihhub/fheroes2 commit `0dbfe9f386f33c2500842f4a59cd284d7f03d541`.

`generate_oracle.py` extracts unchanged getMonsterBaseStrength, Monster::GetMonsterStrength and Troop::GetStrength bodies from the included GPL-2.0-or-later upstream files. Minimal accessors supply data; numeric stat types follow pinned monster_info.h, and speed constants use pinned speed.h directly. The production core receives signed nonnegative native stat inputs: their two-damage sum fits source uint32_t. Its arithmetic and total-strength call chain are checked against the pinned bodies, not native estimateCombatValue.

Build independently, without game assets:

```
cmake -S test/aiIntegration/fheroesStrength -B build-fheroes-strength
cmake --build build-fheroes-strength
ctest --test-dir build-fheroes-strength --output-on-failure
```

4096 deterministic stat/ability samples, one analytic case and five invalid-domain checks run in a single executable. Exact equality is tested on the tested x86_64/GCC configuration, not promised across architectures/compiler floating-point modes. Debug, Release-NDEBUG and undefined/float-cast-overflow sanitizers have been exercised. This validates source arithmetic only. Native mapping, chooser behavior, unsupported-domain fallback and complete AI fidelity require separate evidence.

## Native fixture expectations

`generate_fixture_oracle.py` reuses the same unchanged pinned function bodies to generate `BasicNativeStrengthOracle.h`. Its independent native-input adapter accepts only the vanilla pikeman/archer fixture IDs (0 and 2), with creature base HP/damage/speed/shots and live attack/defense/count. It does not call the port model or native combat-value estimator. Mutable oracle data and the wrapper have translation-unit/thread-local scope. Other abilities must not be admitted by silently broadening the fixture mapper.

The pinned `ai_battle_spell.cpp` records the additional source coefficients used in native migration witnesses: whole-kill premium, nonlethal wake penalty, farewell whole kills, Blind/Curse/Haste/Shield removal and mana conservation. Test fixtures assert the counterfactual decision boundaries as well as actual server effects; they do not merely copy the new chooser output.
