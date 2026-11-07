# Toolchain for *compile-checking* Haiku code on a Linux host.
#
# It compiles every Haiku target (liblumina, server, panel, CLI, engine,
# experiments) against the real Haiku headers with clang's
# x86_64-unknown-haiku target, producing object files only (nothing is
# linked — Haiku's libbe/app_server are not available on Linux).
#
# Prepare the sysroot once with tools/setup-haiku-check-sysroot.sh, then:
#   cmake -S . -B build-check -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/HaikuCompileCheck.toolchain.cmake \
#     -DHAIKU_CHECK_ROOT=$HOME/haiku-check
#   cmake --build build-check
#
# This catches API misuse, missing includes and type errors; it does NOT
# prove runtime behaviour. Real verification happens on Haiku.

set(CMAKE_SYSTEM_NAME Haiku)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT HAIKU_CHECK_ROOT)
	set(HAIKU_CHECK_ROOT "$ENV{HAIKU_CHECK_ROOT}")
endif()
if(NOT HAIKU_CHECK_ROOT)
	message(FATAL_ERROR "Set HAIKU_CHECK_ROOT (see tools/setup-haiku-check-sysroot.sh)")
endif()
set(HAIKU_CHECK_ROOT "${HAIKU_CHECK_ROOT}" CACHE PATH "compile-check sysroot")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES HAIKU_CHECK_ROOT)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET x86_64-unknown-haiku)
set(CMAKE_CXX_COMPILER_TARGET x86_64-unknown-haiku)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(_H "${HAIKU_CHECK_ROOT}/haiku/headers")
execute_process(COMMAND clang -print-resource-dir
	OUTPUT_VARIABLE _CLANG_RESOURCE OUTPUT_STRIP_TRAILING_WHITESPACE)

set(_INC
	"${HAIKU_CHECK_ROOT}/cxx-target"
	"${HAIKU_CHECK_ROOT}/cxx"
	"${_CLANG_RESOURCE}/include"
	"${_H}/os" "${_H}/os/app" "${_H}/os/interface" "${_H}/os/support"
	"${_H}/os/storage" "${_H}/os/kernel" "${_H}/os/media"
	"${_H}/os/translation" "${_H}/os/locale" "${_H}/os/add-ons/input_server"
	"${_H}/os/add-ons/graphics" "${_H}/os/drivers" "${_H}/os/game"
	"${_H}/os/device" "${_H}/os/net" "${_H}/os/mail"
	"${_H}/glibc" "${_H}/posix" "${_H}"
)
set(_FLAGS "-nostdinc -nostdinc++")
foreach(dir ${_INC})
	string(APPEND _FLAGS " -isystem ${dir}")
endforeach()
set(CMAKE_C_FLAGS_INIT "${_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${_FLAGS}")

# Make the Haiku source tree available for the Window Engine.
set(LUMINA_HAIKU_SOURCE_DIR "${HAIKU_CHECK_ROOT}/haiku" CACHE PATH "" FORCE)
set(LUMINA_COMPILE_CHECK_ONLY ON CACHE BOOL "" FORCE)
set(LUMINA_BUILD_HAIKU ON CACHE BOOL "" FORCE)
set(LUMINA_BUILD_ENGINE ON CACHE BOOL "" FORCE)
set(LUMINA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
