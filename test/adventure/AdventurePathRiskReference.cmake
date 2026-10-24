# STAGED ONLY. Include after reviewed AdventureScenarioReference.cmake.
# Requires the separately reviewed risk-consumer production overlay; not q4.
if(ENABLE_NULLKILLER2_AI AND ENABLE_STATIC_LIBS AND TARGET vcmiservercommon)
	if(NOT TARGET vcmi_adventure_scenario_maps)
		message(FATAL_ERROR "Path risk reference requires public adventure scenario maps")
	endif()
	add_custom_target(vcmi_path_risk_maps
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/adventure/generate_path_risk_map.py
			--repo ${CMAKE_SOURCE_DIR} --output ${CMAKE_BINARY_DIR}/bin/test/testdata/AdventureEvaluation
		VERBATIM)
	add_dependencies(vcmi_path_risk_maps vcmi_adventure_scenario_maps)
	add_executable(vcmi_path_risk_reference EXCLUDE_FROM_ALL
		StdInc.cpp main.cpp CVcmiTestConfig.cpp adventure/AdventurePathRiskReference.cpp)
	add_dependencies(vcmi_path_risk_reference vcmi_path_risk_maps)
	target_link_libraries(vcmi_path_risk_reference PRIVATE gtest gmock vcmi Nullkiller2 vcmiservercommon ${SYSTEM_LIBS})
	target_include_directories(vcmi_path_risk_reference PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR} ${GTestSrc} ${GTestSrc}/include ${GMockSrc} ${GMockSrc}/include)
	vcmi_set_output_dir(vcmi_path_risk_reference "")
	enable_pch(vcmi_path_risk_reference)
endif()
