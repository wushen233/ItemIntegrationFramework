cmake_minimum_required(VERSION 3.25)
project(CombatBusPhase1ROffline LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(combatbus_phase1o_fixtures
	 src/CombatBusPrototype.cpp
	 tests/CombatBusPrototypeTests.cpp)

target_include_directories(combatbus_phase1o_fixtures PRIVATE include)
if(MSVC)
	target_compile_options(combatbus_phase1o_fixtures PRIVATE /W4 /WX /permissive-)
else()
	target_compile_options(combatbus_phase1o_fixtures PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME combatbus_phase1o_fixtures COMMAND combatbus_phase1o_fixtures)

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
	add_library(combatbus_iif_mock SHARED
		src/CombatBusPrototype.cpp
		src/CombatBusCABI.cpp)
	target_include_directories(combatbus_iif_mock PRIVATE include tests)
	target_compile_definitions(combatbus_iif_mock PRIVATE IIF_CB_HOST_EXPORTS IIF_CB_TEST_HOOKS)

	add_library(combatbus_test_provider SHARED tests/MockProviderDll.cpp)
	target_include_directories(combatbus_test_provider PRIVATE include)
	add_library(combatbus_test_provider_missing_export SHARED tests/MockProviderDll.cpp)
	target_include_directories(combatbus_test_provider_missing_export PRIVATE include)
	target_compile_definitions(combatbus_test_provider_missing_export PRIVATE
		COMBATBUS_TEST_OMIT_CALLBACK_SHUTDOWN_STATUS)
	set_target_properties(combatbus_test_provider_missing_export PROPERTIES
		OUTPUT_NAME "combatbus_test_provider_missing_export")

	add_executable(combatbus_cross_dll_host
		tests/CrossDllHost.cpp
		tests/MockHostTestHooks.h
		tests/CAbiHeaderSmoke.c)
	target_include_directories(combatbus_cross_dll_host PRIVATE include)

	foreach(target combatbus_iif_mock combatbus_test_provider combatbus_test_provider_missing_export
			combatbus_cross_dll_host)
		if(MSVC)
			target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
		else()
			target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
		endif()
		set_target_properties(${target} PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
			RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
			RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
			RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/RelWithDebInfo"
			RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin/MinSizeRel")
	endforeach()

	add_test(NAME combatbus_cross_dll_abi_and_lifecycle
		COMMAND combatbus_cross_dll_host
			$<TARGET_FILE:combatbus_iif_mock>
			$<TARGET_FILE:combatbus_test_provider>)
	add_test(NAME combatbus_cross_dll_missing_reload_export
		COMMAND combatbus_cross_dll_host
			$<TARGET_FILE:combatbus_iif_mock>
			$<TARGET_FILE:combatbus_test_provider>
			$<TARGET_FILE:combatbus_test_provider_missing_export>
			TestProvider_GetCallbackShutdownStatus)
else()
	message(STATUS "Windows x64 cross-DLL ABI targets are not supported by this build host")
endif()
