# STAGING ONLY. Do not include in qualification3 while its run is frozen.
# Future include must be from test/CMakeLists.txt after source review.
if(ENABLE_NULLKILLER2_AI AND ENABLE_STATIC_LIBS AND TARGET vcmiservercommon)
	find_package(Python3 REQUIRED COMPONENTS Interpreter)
	add_custom_target(vcmi_adventure_interaction_maps
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/adventure/generate_interaction_maps.py
			--repo ${CMAKE_SOURCE_DIR}
			--output ${CMAKE_BINARY_DIR}/bin/test/testdata/AdventureInteractions
		VERBATIM)
	add_executable(vcmi_adventure_interaction_reference EXCLUDE_FROM_ALL
		StdInc.cpp main.cpp CVcmiTestConfig.cpp adventure/AdventureInteractionScenarios.cpp)
	add_dependencies(vcmi_adventure_interaction_reference vcmi_adventure_interaction_maps)
	target_link_libraries(vcmi_adventure_interaction_reference PRIVATE
		gtest gmock vcmi Nullkiller2 vcmiservercommon ${SYSTEM_LIBS})
	target_include_directories(vcmi_adventure_interaction_reference PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR} ${GTestSrc} ${GTestSrc}/include ${GMockSrc} ${GMockSrc}/include)
	vcmi_set_output_dir(vcmi_adventure_interaction_reference "")
	enable_pch(vcmi_adventure_interaction_reference)
endif()
