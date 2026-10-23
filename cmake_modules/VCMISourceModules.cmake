# Internal compilation units, aggregated into the existing public library.
# Object libraries preserve every object (including registration-only objects),
# avoid static archive ordering cycles, and do not change the installed ABI.
# These are build boundaries, not isolated public APIs: shared header changes
# must still rebuild every dependent translation unit. With ENABLE_PCH, each
# module owns its PCH; strict ccache CI uses ENABLE_PCH=OFF.
include_guard(GLOBAL)

function(vcmi_add_source_module owner component source_pattern)
	get_target_property(owner_sources ${owner} SOURCES)
	set(module_sources)
	set(remaining_sources)
	foreach(source IN LISTS owner_sources)
		if(source MATCHES "${source_pattern}" AND source MATCHES "\\.cpp$")
			list(APPEND module_sources "${source}")
		else()
			list(APPEND remaining_sources "${source}")
		endif()
	endforeach()
	# ENABLE_MINIMAL_LIB deliberately omits most gameplay modules.
	if(NOT module_sources)
		return()
	endif()

	set(module "${owner}_${component}")
	add_library(${module} OBJECT ${module_sources})
	set_property(TARGET ${owner} PROPERTY SOURCES ${remaining_sources})
	target_sources(${owner} PRIVATE $<TARGET_OBJECTS:${module}>)
	set_property(TARGET ${module} PROPERTY FOLDER "Modules/${owner}")

	# Evaluate the owner's complete compile usage requirements, including its
	# linked dependencies, without linking back to the owner. TARGET_PROPERTY
	# does not create a build dependency, so this avoids owner/object cycles.
	foreach(property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS COMPILE_FEATURES)
		set_property(TARGET ${module} PROPERTY ${property}
			"$<TARGET_PROPERTY:${owner},${property}>")
	endforeach()

	# Link usage also preserves imported SYSTEM includes and generated dependency
	# ordering. Static AI libraries link back to vcmi; their compile requirements
	# are already represented above, but they must not become object dependencies.
	get_target_property(owner_links ${owner} LINK_LIBRARIES)
	if(owner_links)
		if(owner STREQUAL "vcmi")
			list(REMOVE_ITEM owner_links EmptyAI Nullkiller Nullkiller2 StupidAI BattleAI MMAI)
		endif()
		target_link_libraries(${module} PRIVATE ${owner_links})
	endif()

	# These target-local properties do not propagate through usage requirements.
	foreach(property
		POSITION_INDEPENDENT_CODE C_VISIBILITY_PRESET CXX_VISIBILITY_PRESET
		VISIBILITY_INLINES_HIDDEN C_STANDARD C_STANDARD_REQUIRED C_EXTENSIONS
		CXX_STANDARD CXX_STANDARD_REQUIRED CXX_EXTENSIONS
		COMPILE_FLAGS MSVC_RUNTIME_LIBRARY C_COMPILER_LAUNCHER CXX_COMPILER_LAUNCHER
		PRECOMPILE_HEADERS DISABLE_PRECOMPILE_HEADERS
		INTERPROCEDURAL_OPTIMIZATION)
		get_property(has_property TARGET ${owner} PROPERTY ${property} SET)
		if(has_property)
			get_target_property(value ${owner} ${property})
			set_property(TARGET ${module} PROPERTY ${property} "${value}")
		endif()
	endforeach()

	set(configurations Debug Release RelWithDebInfo MinSizeRel ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
	list(REMOVE_DUPLICATES configurations)
	foreach(configuration IN LISTS configurations)
		string(TOUPPER "${configuration}" configuration)
		foreach(base_property INTERPROCEDURAL_OPTIMIZATION COMPILE_DEFINITIONS)
			set(property "${base_property}_${configuration}")
			get_property(has_property TARGET ${owner} PROPERTY ${property} SET)
			if(has_property)
				get_target_property(value ${owner} ${property})
				set_property(TARGET ${module} PROPERTY ${property} "${value}")
			endif()
		endforeach()
	endforeach()

	# SHARED targets normally receive this definition from CMake automatically;
	# an OBJECT target must use the *original* export symbol, never its own name.
	get_target_property(owner_type ${owner} TYPE)
	if(owner_type STREQUAL "SHARED_LIBRARY" OR owner_type STREQUAL "MODULE_LIBRARY")
		get_target_property(export_symbol ${owner} DEFINE_SYMBOL)
		if(NOT export_symbol)
			set(export_symbol "${owner}_EXPORTS")
		endif()
		target_compile_definitions(${module} PRIVATE "${export_symbol}")
	endif()

	set_property(TARGET ${owner} APPEND PROPERTY VCMI_SOURCE_MODULES ${module})
endfunction()
