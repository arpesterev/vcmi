# Experimental caller probes only. Default configuration remains unchanged.
option(VCMI_BATTLE_AI_CALLER_PROBES "Build opt-in BattleAI caller probes" OFF)
option(VCMI_BATTLE_AI_TEST_TRACE "Enable test-only BattleAI observer in vcmitest" OFF)
if(VCMI_BATTLE_AI_CALLER_PROBES AND NOT ENABLE_BATTLE_AI)
	message(FATAL_ERROR "BattleAI caller probes require ENABLE_BATTLE_AI")
endif()
if(VCMI_BATTLE_AI_TEST_TRACE AND NOT (ENABLE_BATTLE_AI AND VCMI_BATTLE_AI_CALLER_PROBES))
	message(FATAL_ERROR "BattleAI trace requires ENABLE_BATTLE_AI and VCMI_BATTLE_AI_CALLER_PROBES")
endif()
if(ENABLE_BATTLE_AI AND VCMI_BATTLE_AI_CALLER_PROBES)
	target_sources(vcmitest PRIVATE
		battleAI/BattleCallerIntegrationTest.cpp
		battleAI/BattleCallerBenchmark.cpp
	)
	if(VCMI_BATTLE_AI_TEST_TRACE)
		target_compile_definitions(vcmitest PRIVATE VCMI_BATTLE_AI_TEST_TRACE=1)
	endif()
endif()

