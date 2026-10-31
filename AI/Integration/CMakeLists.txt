# Standalone contract checks; these do not run VCMI or any upstream game.
cmake_minimum_required(VERSION 3.24)
project(VCMIAIAdapterContracts LANGUAGES CXX)
enable_testing()
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

option(AI_ADAPTER_SANITIZERS "Check adapter contracts with ASan and UBSan" OFF)
add_executable(ai_adapter_contract ../../CI/ai_adapter_contract.cpp)
add_executable(source_tactics_test ../../CI/source_tactics_test.cpp)
foreach(target IN ITEMS ai_adapter_contract source_tactics_test)
	target_compile_features(${target} PRIVATE cxx_std_20)
	target_link_libraries(${target} PRIVATE Threads::Threads)
	if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -pedantic)
		if(AI_ADAPTER_SANITIZERS)
			target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
			target_link_options(${target} PRIVATE -fsanitize=address,undefined)
		endif()
	endif()
endforeach()

add_test(NAME ai_adapter_protocol COMMAND ai_adapter_contract)
add_test(NAME ai_source_tactics COMMAND source_tactics_test)
add_test(NAME ai_adapter_evidence_checker
	COMMAND ${Python3_EXECUTABLE} -m unittest -v test_ai_backend_acceptance)
set_tests_properties(ai_adapter_evidence_checker PROPERTIES
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../CI")
set_tests_properties(ai_adapter_protocol ai_source_tactics ai_adapter_evidence_checker PROPERTIES TIMEOUT 30)
