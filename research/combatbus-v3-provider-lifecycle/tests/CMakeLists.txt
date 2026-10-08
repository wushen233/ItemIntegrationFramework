cmake_minimum_required(VERSION 3.25)
project(CombatBusProviderLifecycleOffline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "Provider lifecycle cross-DLL fixture requires Windows x64")
endif()

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(PROBE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(ABI_SOURCE_ROOT "${REPOSITORY_ROOT}/src/CombatBusV3")

add_library(iif_provider_lifecycle_host SHARED
	"${ABI_SOURCE_ROOT}/CombatBusPrototype.cpp"
	"${ABI_SOURCE_ROOT}/CombatBusCABI.cpp")
target_include_directories(iif_provider_lifecycle_host PRIVATE "${ABI_SOURCE_ROOT}")
target_compile_definitions(iif_provider_lifecycle_host PRIVATE
	IIF_CB_HOST_EXPORTS IIF_CB_PRODUCTION_HOST)

add_executable(iif_provider_lifecycle_host_test
	OfflineLifecycleHost.cpp
	"${PROBE_SOURCE_ROOT}/ProviderLifecycleScenario.cpp")
target_include_directories(iif_provider_lifecycle_host_test PRIVATE
	"${PROBE_SOURCE_ROOT}" "${ABI_SOURCE_ROOT}")

foreach(target iif_provider_lifecycle_host iif_provider_lifecycle_host_test)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
	endif()
	set_target_properties(${target} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endforeach()

enable_testing()
add_test(NAME provider_lifecycle_cross_dll
	COMMAND iif_provider_lifecycle_host_test $<TARGET_FILE:iif_provider_lifecycle_host>)
add_test(NAME provider_lifecycle_no_dispatch_static
	COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
		"${CMAKE_CURRENT_SOURCE_DIR}/VerifyLifecycleNoDispatch.ps1"
		-RepositoryRoot "${REPOSITORY_ROOT}" -ProjectRoot "${CMAKE_CURRENT_SOURCE_DIR}/..")
