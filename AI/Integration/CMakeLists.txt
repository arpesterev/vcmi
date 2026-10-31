# Standalone contract checks; these do not run VCMI or any upstream game.
cmake_minimum_required(VERSION 3.24)
project(VCMIAIAdapterContracts LANGUAGES CXX)
enable_testing()
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(ai_adapter_contract ../../CI/ai_adapter_contract.cpp)
target_compile_features(ai_adapter_contract PRIVATE cxx_std_20)
target_link_libraries(ai_adapter_contract PRIVATE Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(ai_adapter_contract PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
option(AI_ADAPTER_SANITIZERS "Check adapter contracts with ASan and UBSan" OFF)
if(AI_ADAPTER_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(ai_adapter_contract PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(ai_adapter_contract PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME ai_adapter_protocol COMMAND ai_adapter_contract)
add_test(NAME ai_adapter_evidence_checker
	COMMAND ${Python3_EXECUTABLE} -m unittest -v test_ai_backend_acceptance)
set_tests_properties(ai_adapter_evidence_checker PROPERTIES
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../CI")
set_tests_properties(ai_adapter_protocol ai_adapter_evidence_checker PROPERTIES TIMEOUT 30)
