# HoMM3 forecast test source and licensing

## Reconstructed source and derived control primitives

The source bodies in SourcePrefixBodies.inc are copied verbatim from the public reconstruction **sushi-shi/homm3-decomp**, commit **e12906accf96c2662b1abadbc49ddc111991bf21**. The reconstructed source repository publishes these files under **CC0 1.0 Universal**:

- [Pinned upstream LICENSE](https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/LICENSE), Git blob 0e259d42c996742e9e3cba14c677129b2c1b6311.
- The unchanged license text is included as LICENSE.CC0-1.0.
- [isIncapacitated / cannotAttack, include/army.h1324–1349](https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/include/army.h#L1324).
- [getTotalHitPoints / setAIExpectedDamage, src/army.cpp2384–2402](https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/army.cpp#L2384).
- [simulateActions, src/ai.cpp2196–2235](https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/ai.cpp#L2196).

MoveOrder.h/.cpp and OwnershipPrefix.h/.cpp implement the corresponding source control primitives with explicit host inputs. Their source references also include [getMoveOrder/findMoveOrder, ai.cpp571–630](https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/ai.cpp#L571) and [func_moves_before, ai.cpp2392–2400](https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/ai.cpp#L2392). These files, the new NativeSnapshot adapter, SourcePrefixReference harness, and CI/homm3_forecast_prefix_test.cpp carry explicit CC0-1.0 identifiers. The harness replaces choice/damage mechanisms with controlled test stubs; it is not original game code or a retail oracle.

The original function bodies are not relabeled GPL. This notice attributes the reconstruction to its upstream project and contributors without claiming authorship of its reconstructed functions. CC0's limitations and disclaimers, including its treatment of third-party rights, remain in the included text. No retail binary, game data or assets are distributed or licensed by this notice. Retail behavior/runtime traces remain unverified.

## VCMI native fixture integration

HoMM3ForecastQueueTest.cpp uses VCMI's existing native fixture and setup patterns from [BattleTestFixture.cpp](https://github.com/arpesterev/vcmi/blob/747784ed/test/server/battles/BattleTestFixture.cpp) and [NativePortBattleTest.cpp](https://github.com/arpesterev/vcmi/blob/747784ed/test/aiIntegration/NativePortBattleTest.cpp). Those files identify their license as GNU GPL version2 or later. The new VCMI-specific fixture accordingly retains **GPL-2.0-or-later**, as stated in its header; this is not asserted as the source reconstruction's license. The existing VCMI repository license.txt and AUTHORS remain unchanged.

The linked vcmitest executable remains part of the GPL-licensed VCMI project. This separation preserves the CC0 provenance of copied/derived reconstruction material and the GPL provenance of VCMI-specific fixture integration. No project-wide relicensing is performed.

## Event-lifecycle fixture increment

Lifecycle.h and NativeLifecycle.h are new CC0-1.0 test-support implementations of the explicit host event/initial-roster mapping. They do not copy retail code or assert retail event identity. Source slot identity follows [loadArmies, cmbtmgr.cpp918–1010](https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/cmbtmgr.cpp#L918), and the second-phase latch follows [nextArmy, cmbtmgr.cpp1683–1740](https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/cmbtmgr.cpp#L1683). Source death retains the army entry and marks Immobilized: [army.cpp2818–2889](https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/army.cpp#L2818).

HoMM3ForecastLifecycleTest.cpp is GPL-2.0-or-later, derived from the VCMI BattleTestFixture setup and a test bridge modeling VCMI's pre/post-apply event dispatch contract. It executes native game-state pack application but does not link or execute the actual GUI client dispatcher. No production forecast or battle-AI decision policy is connected by this increment.

Created units remain unsupported in this adapter. The reconstruction's [addArmy, command.cpp2712–2756](https://github.com/sushi-shi/homm3-decomp/blob/e12906accf96c2662b1abadbc49ddc111991bf21/src/command.cpp#L2712) reuses the first unused or dead/immobilized/summoned slot among20, which differs from native monotonically appended unit identities. This increment does not claim that mapping is complete.
