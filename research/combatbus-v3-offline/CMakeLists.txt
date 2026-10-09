cmake_minimum_required(VERSION 3.25)
project(CombatBusPhase1ROffline LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(combatbus_phase1o_fixtures
	 ../../src/CombatBusV3/CombatBusPrototype.cpp
	 tests/CombatBusPrototypeTests.cpp)

target_include_directories(combatbus_phase1o_fixtures PRIVATE include ../../src/CombatBusV3)
target_compile_definitions(combatbus_phase1o_fixtures PRIVATE IIF_COMBATBUS_OFFLINE_TESTING)
set(COMBATBUS_TEST_BINARY_DIR "${CMAKE_BINARY_DIR}/bin")
if(DEFINED COMBATBUS_EXTERNAL_BINARY_DIR)
	set(COMBATBUS_TEST_BINARY_DIR "${COMBATBUS_EXTERNAL_BINARY_DIR}")
endif()
set_target_properties(combatbus_phase1o_fixtures PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${COMBATBUS_TEST_BINARY_DIR}")
if(MSVC)
	target_compile_options(combatbus_phase1o_fixtures PRIVATE /W4 /WX /permissive-)
else()
	target_compile_options(combatbus_phase1o_fixtures PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME combatbus_phase1o_fixtures COMMAND combatbus_phase1o_fixtures)

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
	add_library(combatbus_iif_mock SHARED
		../../src/CombatBusV3/CombatBusPrototype.cpp
		../../src/CombatBusV3/CombatBusCABI.cpp)
	target_include_directories(combatbus_iif_mock PRIVATE include tests ../../src/CombatBusV3)
	target_compile_definitions(combatbus_iif_mock PRIVATE IIF_CB_HOST_EXPORTS IIF_CB_TEST_HOOKS
		IIF_CB_TEST_HOST_CONTROL IIF_COMBATBUS_OFFLINE_TESTING)
	add_library(combatbus_production_contract SHARED
		../../src/CombatBusV3/CombatBusPrototype.cpp
		../../src/CombatBusV3/CombatBusCABI.cpp)
	target_include_directories(combatbus_production_contract PRIVATE ../../src/CombatBusV3)
	target_compile_definitions(combatbus_production_contract PRIVATE IIF_CB_HOST_EXPORTS IIF_CB_PRODUCTION_HOST)
	add_executable(combatbus_production_export_probe tests/ProductionExportProbe.cpp)
	target_include_directories(combatbus_production_export_probe PRIVATE ../../src/CombatBusV3)

	add_library(combatbus_test_provider SHARED tests/MockProviderDll.cpp)
	target_include_directories(combatbus_test_provider PRIVATE include ../../src/CombatBusV3)
	target_compile_definitions(combatbus_test_provider PRIVATE IIF_CB_TEST_HOST_CONTROL)
	add_library(combatbus_test_provider_missing_export SHARED tests/MockProviderDll.cpp)
	target_include_directories(combatbus_test_provider_missing_export PRIVATE include ../../src/CombatBusV3)
	target_compile_definitions(combatbus_test_provider_missing_export PRIVATE
		COMBATBUS_TEST_OMIT_CALLBACK_SHUTDOWN_STATUS IIF_CB_TEST_HOST_CONTROL)
	set_target_properties(combatbus_test_provider_missing_export PROPERTIES
		OUTPUT_NAME "combatbus_test_provider_missing_export")

	add_executable(combatbus_cross_dll_host
		tests/CrossDllHost.cpp
		tests/MockHostTestHooks.h
		tests/CAbiHeaderSmoke.c
		tests/StartupTrace.h)
	target_include_directories(combatbus_cross_dll_host PRIVATE include ../../src/CombatBusV3)
	target_compile_definitions(combatbus_cross_dll_host PRIVATE IIF_CB_TEST_HOST_CONTROL)
	add_executable(combatbus_startup_probe tests/StartupProbe.cpp tests/StartupTrace.h)
	target_include_directories(combatbus_startup_probe PRIVATE tests)

	foreach(target combatbus_iif_mock combatbus_production_contract combatbus_production_export_probe
			combatbus_test_provider combatbus_test_provider_missing_export combatbus_cross_dll_host
			combatbus_startup_probe)
		if(MSVC)
			target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
		else()
			target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
		endif()
		set_target_properties(${target} PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${COMBATBUS_TEST_BINARY_DIR}"
			LIBRARY_OUTPUT_DIRECTORY "${COMBATBUS_TEST_BINARY_DIR}")
	endforeach()
	add_test(NAME combatbus_cross_dll_abi_and_lifecycle
		COMMAND combatbus_cross_dll_host
			$<TARGET_FILE:combatbus_iif_mock>
			$<TARGET_FILE:combatbus_test_provider>)
	set(host_trace "${CMAKE_BINARY_DIR}/cross-dll-startup.trace")
	add_test(NAME combatbus_cross_dll_clear_startup_trace
		COMMAND ${CMAKE_COMMAND} -E rm -f "${host_trace}")
	set_tests_properties(combatbus_cross_dll_abi_and_lifecycle PROPERTIES
		DEPENDS combatbus_cross_dll_clear_startup_trace
		ENVIRONMENT "IIF_COMBATBUS_PHASE1V_MARKER=${host_trace}"
		TIMEOUT 45)
	add_test(NAME combatbus_cross_dll_missing_reload_export
		COMMAND combatbus_cross_dll_host
			$<TARGET_FILE:combatbus_iif_mock>
			$<TARGET_FILE:combatbus_test_provider>
			$<TARGET_FILE:combatbus_test_provider_missing_export>
			TestProvider_GetCallbackShutdownStatus)
	set(negative_trace "${CMAKE_BINARY_DIR}/cross-dll-missing-export-startup.trace")
	add_test(NAME combatbus_cross_dll_missing_export_clear_startup_trace
		COMMAND ${CMAKE_COMMAND} -E rm -f "${negative_trace}")
	set_tests_properties(combatbus_cross_dll_missing_reload_export PROPERTIES
		DEPENDS combatbus_cross_dll_missing_export_clear_startup_trace
		ENVIRONMENT "IIF_COMBATBUS_PHASE1V_MARKER=${negative_trace}"
		TIMEOUT 45)
	set(probe_trace "${CMAKE_BINARY_DIR}/startup-probe.trace")
	add_test(NAME combatbus_startup_probe_clear_trace
		COMMAND ${CMAKE_COMMAND} -E rm -f "${probe_trace}")
	add_test(NAME combatbus_startup_probe COMMAND combatbus_startup_probe)
	set_tests_properties(combatbus_startup_probe PROPERTIES
		DEPENDS combatbus_startup_probe_clear_trace
		ENVIRONMENT "IIF_COMBATBUS_PHASE1V_MARKER=${probe_trace}")
	add_test(NAME combatbus_production_interface_and_exports
		COMMAND combatbus_production_export_probe $<TARGET_FILE:combatbus_production_contract>)
	add_test(NAME combatbus_no_hook_structure
		COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/NoHookStructureCheck.ps1" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
	add_test(NAME observation_callsite_abi
		COMMAND "${COMBATBUS_TEST_BINARY_DIR}/Release/observation_callsite_abi_fixtures.exe")
	add_test(NAME observation_callsite_rel32
		COMMAND "${COMBATBUS_TEST_BINARY_DIR}/Release/observation_callsite_rel32_fixtures.exe")
	add_test(NAME observation_mpsc_ring
		COMMAND "${COMBATBUS_TEST_BINARY_DIR}/Release/observation_mpsc_ring_fixtures.exe")
	add_test(NAME observation_legacy_isolation_contract
		COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/observation/VerifyLegacyIsolation.ps1"
			"${CMAKE_CURRENT_SOURCE_DIR}/../.."
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/observation/ObservationBuildContract.json")
	set_tests_properties(observation_callsite_abi observation_callsite_rel32 observation_mpsc_ring
		observation_legacy_isolation_contract PROPERTIES TIMEOUT 30)
else()
	message(STATUS "Windows x64 cross-DLL ABI targets are not supported by this build host")
endif()
