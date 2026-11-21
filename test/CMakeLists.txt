include(GoogleTest)
include(CheckCXXCompilerFlag)

set(googleTest_Dir ${CMAKE_CURRENT_SOURCE_DIR}/googletest)
if(EXISTS ${googleTest_Dir})
	set(GTestSrc ${googleTest_Dir}/googletest)
	set(GMockSrc ${googleTest_Dir}/googlemock)
else()
	message(FATAL_ERROR "No googletest src dir found!")
endif()

set(test_SRCS
 		StdInc.cpp
 		main.cpp
 		CMemoryBufferTest.cpp
 		CVcmiTestConfig.cpp
 		JsonComparer.cpp

 		battle/BattleHexTest.cpp
 		battle/CBattleInfoCallbackTest.cpp
 		battle/CHealthTest.cpp
		battle/CUnitStateTest.cpp
		battle/OrdinaryUnitStoreAtomicMutationTest.cpp
		battle/OrdinaryAmmoRoutingTest.cpp
		battle/OrdinaryAmmoEnvironmentTest.cpp
		battle/OrdinaryDescriptorEffectTest.cpp
		battle/OrdinaryAmmoPrototypeMirrorTest.cpp
		battle/OrdinaryUnitViewStoreTest.cpp
		battle/OrdinaryUnitStoreLifecycleTest.cpp
		battle/HypotheticCopyStateTest.cpp
		battle/HypotheticExchangeBoundaryTest.cpp
		battle/HypotheticExchangeEntryTest.cpp
		prototype/NativeSnapshotAccessTest.cpp
		prototype/OrdinaryPreviewDefinitionGuardTest.cpp
		prototype/OwnedFrameTest.cpp
		prototype/OwnedBonusGraphSessionTest.cpp
		prototype/OwnedCacheCaptureTest.cpp
		prototype/OwnedCacheSessionTest.cpp
		prototype/SealedBonusSnapshotTest.cpp
		prototype/ClosedBonusCaptureTest.cpp
		prototype/OwnedTypedUnitEffectsTest.cpp
		prototype/OwnedTypedRemovalTest.cpp
		prototype/OwnedDurationExpiryTest.cpp
		prototype/OwnedStatePacketTest.cpp
		prototype/OwnedResurrectionPacketTest.cpp
		prototype/OwnedMixedUnitPacketTest.cpp
		prototype/OwnedTurnFacadeTest.cpp
		prototype/OwnedBattleBearerTest.cpp
		prototype/OwnedRoundLifecycleTest.cpp
		prototype/OwnedDefendEffectsTest.cpp
		prototype/OwnedComponentProducerTest.cpp
		prototype/OwnedScalarProducerTest.cpp
		prototype/OwnedDetachedUnitTest.cpp
		prototype/OwnedDetachedAliasTest.cpp
		prototype/OwnedCreatureContextTest.cpp
		prototype/NativeAddInitOracleTest.cpp
		prototype/OwnedArenaMaterializationContractTestV2.cpp
		prototype/OwnedLeafAppendTest.cpp
		prototype/OwnedUnsupportedFacadeTest.cpp
		prototype/OwnedArenaLifetimeTest.cpp
		prototype/OwnedArenaNativeParityTest.cpp
		prototype/OwnedCreatureTest.cpp
		prototype/OwnedCreatureCacheTest.cpp
		battle/CUnitStateMagicTest.cpp
		battle/battle_UnitTest.cpp

		bonus/BonusSystemTest.cpp

		combatScripts/CombatScriptDescriptionTest.cpp

		entity/CArtifactTest.cpp
		entity/CCreatureTest.cpp
		entity/StackExperienceBonusesTest.cpp
		entity/CFactionTest.cpp
		entity/CHeroClassTest.cpp
		entity/CHeroTest.cpp
		entity/CSkillTest.cpp

		events/ApplyDamageTest.cpp
		events/EventBusTest.cpp

		filesystem/FilesystemTest.cpp
		filesystem/SavegamePathTest.cpp

		game/BattleSpellCastTest.cpp
		game/HeroRecruitmentTest.cpp
		game/HeroMovementTest.cpp
		game/HeroSecondarySkillsTest.cpp
		game/MovementCostTest.cpp
		game/SaveGameTest.cpp
		game/VictoryLossTest.cpp

		json/JsonTests.cpp

		serializer/SaveFileTest.cpp

		map/CMapEditManagerTest.cpp
		map/CMapFormatTest.cpp
		map/CGArtifactRegressionTest.cpp
		map/MapScriptTest.cpp
		map/MapObjectDrawOrderTest.cpp
		map/PlayerFactionsTest.cpp
		map/TinyH3MBuilderTest.cpp

		quest/QuestTest.cpp
		quest/QuestAssertions.cpp
		quest/QuestScenarios.cpp
		quest/CQuestLoaderTest.cpp
		quest/CQuestSeerTest.cpp
		quest/CQuestSeerMultiTest.cpp
		quest/CQuestVisibilityTest.cpp
		quest/CQuestMarkerTest.cpp
		quest/CQuestGuardTest.cpp
		quest/CQuestBorderTest.cpp
		quest/CQuestGateTest.cpp

		netpacks/NetPackFixture.cpp

		spells/AbilityCasterTest.cpp
		spells/AdventureSpellEffectTest.cpp
		spells/CSpellTest.cpp
 		spells/TargetConditionTest.cpp

		spells/effects/EffectFixture.cpp
 		spells/effects/CatapultTest.cpp
 		spells/effects/CloneTest.cpp
 		spells/effects/DamageTest.cpp
 		spells/effects/DispelTest.cpp
 		spells/effects/HealTest.cpp
 		spells/effects/DemonSummonTest.cpp
		spells/effects/MoatTest.cpp
		spells/effects/ObstacleTest.cpp
		spells/effects/RemoveObstacleTest.cpp
		spells/effects/SacrificeTest.cpp
 		spells/effects/SummonTest.cpp
 		spells/effects/TeleportTest.cpp
 		spells/effects/TimedTest.cpp

 		spells/targetConditions/AbsoluteSpellConditionTest.cpp
 		spells/targetConditions/AbsoluteLevelConditionTest.cpp
 		spells/targetConditions/BonusConditionTest.cpp
 		spells/targetConditions/CreatureConditionTest.cpp
 		spells/targetConditions/ElementalConditionTest.cpp
 		spells/targetConditions/HealthValueConditionTest.cpp
 		spells/targetConditions/ImmunityNegationConditionTest.cpp
 		spells/targetConditions/NormalLevelConditionTest.cpp
 		spells/targetConditions/NormalSpellConditionTest.cpp
 		spells/targetConditions/ReceptiveFeatureConditionTest.cpp
		spells/targetConditions/ResistanceConditionTest.cpp
 		spells/targetConditions/SpellEffectConditionTest.cpp
 		spells/targetConditions/TargetConditionItemFixture.cpp

		server/battles/AcidBreathTest.cpp
		server/battles/ActionOptionsTest.cpp
		server/battles/AttackSequenceTest.cpp
		server/battles/BallistaDamageTest.cpp
		server/battles/BattleTestFixture.cpp
		server/battles/CombatEventTriggerTest.cpp
		server/battles/DamageCalculatorTest.cpp
		server/battles/BindTest.cpp
		server/battles/DeathStareTest.cpp
		server/battles/DestructionTest.cpp
		server/battles/EnchantedTest.cpp
		server/battles/FireShieldTest.cpp
		server/battles/HotaAbilitiesTest.cpp
		server/battles/HypnotizeTest.cpp
		server/battles/LifeDrainTest.cpp
		server/battles/WarMachineResurrectionTest.cpp
		server/battles/ResurrectionCasualtiesTest.cpp
		server/battles/MeleeActionTest.cpp
		server/battles/MoatActionTest.cpp
		server/battles/NoTargetActionTest.cpp
		server/battles/RangedActionTest.cpp
		server/battles/RebirthTest.cpp
		server/battles/SoulStealTest.cpp
		server/battles/SpellcastActionTest.cpp
		server/battles/SpellTargetStepTest.cpp
		server/battles/SummonGuardiansTest.cpp
		server/battles/TurnOrderTest.cpp
		server/battles/TransmutationTest.cpp
		server/battles/WalkActionTest.cpp
		server/activities/ActivityProcessorTest.cpp
		server/ArmyManagementTest.cpp

		texts/TextOperationsTest.cpp

		mock/BattleFake.cpp
		mock/GameHandlerTestClient.cpp
		mock/GameHandlerTestServer.cpp
		mock/mock_IGameEventCallback.cpp
 		mock/mock_MapService.cpp
 		mock/mock_MapServiceTinyH3M.cpp
 		mock/mock_BonusBearer.cpp
		mock/mock_CPSICallback.cpp
		mock/TinyMapGameTest.cpp
		mock/TinyH3MWriter.cpp
		mock/TinyH3MBuilder.cpp
)

set(test_HEADERS
 		StdInc.h

 		CVcmiTestConfig.h
		JsonComparer.h


 		netpacks/NetPackFixture.h

		spells/effects/EffectFixture.h

		spells/targetConditions/TargetConditionItemFixture.h

		server/battles/BattleTestFixture.h

		mock/BattleFake.h
		mock/GameHandlerTestClient.h
		mock/GameHandlerTestServer.h
		mock/mock_BonusBearer.h
		mock/mock_CStack.h
		mock/mock_IGameEventCallback.h
 		mock/mock_MapService.h
 		mock/mock_MapServiceTinyH3M.h
		mock/mock_BonusBearer.h
		mock/TownFake.h
		mock/TinyMapGameTest.h
		mock/TinyH3MWriter.h
		mock/TinyH3MBuilder.h

		quest/QuestTest.h
		quest/QuestAssertions.h
		quest/QuestScenarios.h
)

if(ENABLE_BATTLE_AI)
	file(GLOB_RECURSE BATTLE_AI_TEST_SRCS "../AI/BattleAI/*.cpp")
	list(FILTER BATTLE_AI_TEST_SRCS EXCLUDE REGEX ".*main\\.cpp$")

	list(APPEND test_SRCS
		${BATTLE_AI_TEST_SRCS}
		battleAI/BattleEvaluatorTest.cpp
		battleAI/SpellTargetsEvaluatorTest.cpp
		aiIntegration/NativePortBattleTest.cpp
		aiIntegration/FHeroes2WideCautiousTest.cpp
		aiIntegration/FHeroes2RetreatCenterRingTest.cpp
	aiIntegration/HoMM3OrdinaryTwoPassNativeTest.cpp
	aiIntegration/HoMM3LiveShooterNativeTest.cpp
	aiIntegration/HoMM3GroundNativeTest.cpp
	aiIntegration/HoMM3TiedLandingTest.cpp
	aiIntegration/HoMM3GroundControllerTest.cpp
	aiIntegration/HoMM3OwnHeroGroundTest.cpp
	aiIntegration/HoMM3FailedSiegeTest.cpp
	aiIntegration/HoMM3ForecastRetreatTest.cpp
	aiIntegration/HoMM3MixedNativeTest.cpp
	aiIntegration/FHeroes2NativeStrengthTest.cpp
	aiIntegration/FHeroes2NativeThreatTest.cpp
	aiIntegration/FHeroes2DoubleAttackTest.cpp
	aiIntegration/FHeroes2WolfStrengthTest.cpp
	aiIntegration/FHeroes2DoubleShotTest.cpp
	aiIntegration/FHeroes2AreaResistanceTest.cpp
	aiIntegration/FHeroes2FarewellAreaTest.cpp
	aiIntegration/HoMM3PrayerTest.cpp
	aiIntegration/HoMM3PrayerRemovalTest.cpp
	aiIntegration/HoMM3MixedDefendTest.cpp
		aiIntegration/homm3Defend/NativeDefendTest.cpp
		aiIntegration/FHeroes2CloneCascadeTest.cpp
		aiIntegration/FHeroes2BerserkAmmoTest.cpp
		aiIntegration/FHeroes2ShieldAmmoTest.cpp
		aiIntegration/FHeroes2CloneAmmoTest.cpp
		aiIntegration/FHeroes2FireballResistanceTest.cpp
		aiIntegration/homm3Defend/DamageCapabilityTest.cpp
		aiIntegration/homm3Defend/CurrentDefendTest.cpp
		aiIntegration/homm3Exchange/GeometryNativeTest.cpp
		aiIntegration/HoMM3ForecastQueueTest.cpp
		aiIntegration/HoMM3ForecastLifecycleTest.cpp
		aiIntegration/homm3Forecast/MoveOrder.cpp
		aiIntegration/homm3Forecast/OwnershipPrefix.cpp
		aiIntegration/MMAINNOutputValidationTest.cpp
	)
endif()

if(ENABLE_NULLKILLER2_AI)
	list(APPEND test_SRCS
		nullkiller2/AIGatewayDialogTest.cpp
		nullkiller2/AIGatewayMovementTest.cpp
		nullkiller2/AIUtilityTest.cpp
		nullkiller2/Analyzers/ArmyManagerTest.cpp
		nullkiller2/Analyzers/DangerHitMapAnalyzerTest.cpp
		nullkiller2/Analyzers/HeroManagerTest.cpp
		nullkiller2/Analyzers/ObjectClusterizerTest.cpp
		nullkiller2/Behaviors/DefenceBehaviorTest.cpp
		nullkiller2/Behaviors/EscapeBehaviorTest.cpp
		nullkiller2/Behaviors/GatherArmyBehaviorTest.cpp
		nullkiller2/Behaviors/OneWayPortalBehaviorTest.cpp
		nullkiller2/Behaviors/RecruitHeroBehaviorTest.cpp
		nullkiller2/Behaviors/WaterExplorationBehaviorTest.cpp
		nullkiller2/Engine/PriorityEvaluatorTest.cpp
		nullkiller2/Engine/ResourceTradingTurnTest.cpp
		nullkiller2/Engine/TaskFailureTest.cpp
		nullkiller2/Goals/BuyArmyTest.cpp
		nullkiller2/Goals/CompleteQuestTest.cpp
		nullkiller2/Goals/CompositionTest.cpp
		nullkiller2/Goals/ExecuteHeroChainTest.cpp
		nullkiller2/Goals/ExploreNeighbourTileTest.cpp
		nullkiller2/Helpers/ExplorationHelperTest.cpp
		nullkiller2/Helpers/TinyH3MDimensionDoorExplorationTest.cpp
		nullkiller2/Pathfinding/AIPathNodePoolTest.cpp
		nullkiller2/Pathfinding/ArmyLossTest.cpp
		nullkiller2/Pathfinding/DimensionDoorActionTest.cpp
		nullkiller2/Pathfinding/QuestActionTest.cpp
	)

	list(APPEND test_HEADERS
		nullkiller2/Nulkiller2TestUtils.h
		nullkiller2/NullkillerTest.h
		nullkiller2/ScriptedMovementClient.h
	)
endif()

assign_source_group(${test_SRCS} ${test_HEADERS})

set(mock_HEADERS
		mock/mock_battle_IBattleState.h
		mock/mock_battle_Unit.h
		mock/mock_Creature.h
		mock/mock_CreatureService.h
		mock/mock_IBattleInfoCallback.h
		mock/mock_scripting_Pool.h
		mock/mock_spells_Mechanics.h
		mock/mock_spells_Problem.h
		mock/mock_spells_Spell.h
		mock/mock_UnitEnvironment.h
		mock/mock_UnitInfo.h
		mock/mock_vstd_RNG.h
		mock/mock_CPSICallback.h
)

if(MSVC)
	set(gtest_force_shared_crt ON CACHE BOOL "Use shared (DLL) run-time lib even when Google Test is built as static lib." FORCE)
endif()
check_cxx_compiler_flag(-Wcharacter-conversion CHARCONV)
if(CHARCONV)
	add_compile_options(-Wno-error=character-conversion) #Used in googletest
endif()
check_cxx_compiler_flag(-Wimplicit-int-float-conversion CONV)
if(CONV)
	add_compile_options(-Wno-error=implicit-int-float-conversion) #Used in googletest
endif()
check_cxx_compiler_flag(-Wdeprecated-copy-with-user-provided-copy COPY)
if(COPY)
	add_compile_options(-Wno-deprecated-copy-with-user-provided-copy) #Used in googletest
endif()
check_cxx_compiler_flag(-Wvirtual-move-assign MOVE_ASSIGN)
if(MOVE_ASSIGN)
	add_compile_options(-Wno-error=virtual-move-assign) #GCC is too strict here
endif()
add_subdirectory_with_folder("3rdparty" googletest EXCLUDE_FROM_ALL)

add_executable(vcmitest ${test_SRCS} ${test_HEADERS} ${mock_HEADERS})
# Tests link the constituent object libraries rather than the facade: that keeps
# lib globals single-instance and lets classes needed only by tests stay
# unexported, since static linking ignores visibility.
if(ENABLE_NULLKILLER2_AI)
	# Nullkiller tests use internal AI symbols, so embed its objects directly.
	target_sources(vcmitest PRIVATE $<TARGET_OBJECTS:Nullkiller2>)
endif()

set(vcmitest_LIBS gtest gmock vcmiMain vcmiservercommon ${SYSTEM_LIBS})
foreach(optional_vcmitest_target IN ITEMS vcmiLua EmptyAI StupidAI MMAI TBB::tbb)
	if(TARGET ${optional_vcmitest_target})
		list(APPEND vcmitest_LIBS ${optional_vcmitest_target})
	endif()
endforeach()
target_link_libraries(vcmitest PRIVATE ${vcmitest_LIBS})

target_include_directories(vcmitest
		PUBLIC	${CMAKE_CURRENT_SOURCE_DIR}
		PRIVATE	${GTestSrc}
		PRIVATE	${GTestSrc}/include
		PRIVATE	${GMockSrc}
		PRIVATE	${GMockSrc}/include
)

# Running tests one by one using ctest not recommended due to vcmi having
# slow global initialization.
gtest_discover_tests(vcmitest
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")

vcmi_set_output_dir(vcmitest "")

enable_pch(vcmitest)

file (GLOB_RECURSE testdata "testdata/*.*")
foreach(resource ${testdata})
	get_filename_component(filename ${resource} NAME)
	get_filename_component(dir ${resource} DIRECTORY)
	get_filename_component(dirname ${dir} NAME)
	set (output "")
	while(NOT ${dirname} STREQUAL testdata)
		get_filename_component(path_component ${dir} NAME)
		set (output "${path_component}/${output}")
		get_filename_component(dir ${dir} DIRECTORY)
		get_filename_component(dirname ${dir} NAME)
	endwhile()
	set(output "${CMAKE_BINARY_DIR}/bin/test/testdata/${output}/${filename}")
	configure_file(${resource} ${output} COPYONLY)
endforeach()

add_subdirectory(aiIntegration/fheroesStrength)
add_subdirectory(aiIntegration/fheroesThreat)
