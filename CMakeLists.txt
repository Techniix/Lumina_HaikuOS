cmake_minimum_required(VERSION 3.16)

project(LuminaHaiku VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
	set(LUMINA_ON_HAIKU ON)
else()
	set(LUMINA_ON_HAIKU OFF)
endif()

option(LUMINA_BUILD_TESTS "Build unit tests" ON)
option(LUMINA_BUILD_HAIKU "Build Haiku components (server, panel, CLI)"
	${LUMINA_ON_HAIKU})
option(LUMINA_BUILD_ENGINE "Build the app_server Window Engine add-on"
	${LUMINA_ON_HAIKU})
option(LUMINA_BUILD_EXPERIMENTS "Build visual-effects research prototypes" ON)
option(LUMINA_COMPILE_CHECK_ONLY
	"Compile Haiku targets to objects only (cross compile-check from a non-Haiku host)"
	OFF)
option(LUMINA_WERROR "Treat warnings as errors" OFF)

set(LUMINA_HAIKU_SOURCE_DIR "" CACHE PATH
	"Haiku source tree matching the running system (needed for the Window Engine)")
set(LUMINA_HAIKU_ABI "" CACHE STRING
	"Haiku revision the engine is built for, e.g. hrev59000 (auto-detected on Haiku)")

include(LuminaHelpers)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)
if(LUMINA_WERROR)
	add_compile_options(-Werror)
endif()

add_subdirectory(src/core)

if(LUMINA_BUILD_HAIKU)
	add_subdirectory(src/liblumina)
	add_subdirectory(src/server)
	add_subdirectory(src/cli)
	add_subdirectory(src/panel)
	add_subdirectory(src/mode)
	add_subdirectory(src/input)
endif()

if(LUMINA_BUILD_ENGINE)
	add_subdirectory(src/engine)
endif()

if(LUMINA_BUILD_EXPERIMENTS AND LUMINA_BUILD_HAIKU)
	add_subdirectory(experiments)
endif()

if(LUMINA_BUILD_TESTS AND NOT LUMINA_COMPILE_CHECK_ONLY)
	enable_testing()
	add_subdirectory(tests)
endif()

if(LUMINA_BUILD_HAIKU AND NOT LUMINA_COMPILE_CHECK_ONLY)
	include(LuminaInstall)
endif()

message(STATUS "Lumina ${PROJECT_VERSION}: haiku=${LUMINA_ON_HAIKU} "
	"components=${LUMINA_BUILD_HAIKU} engine=${LUMINA_BUILD_ENGINE} "
	"compile-check=${LUMINA_COMPILE_CHECK_ONLY}")
