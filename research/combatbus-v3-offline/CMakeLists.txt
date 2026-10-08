cmake_minimum_required(VERSION 3.25)
project(CombatBusPhase1OOffline LANGUAGES CXX)

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
