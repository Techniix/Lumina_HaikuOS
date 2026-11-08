# Lumina Window Engine: an app_server decorator add-on.
#
# It is compiled against app_server's internal headers, so it needs a Haiku
# source tree matching the target system (LUMINA_HAIKU_SOURCE_DIR) and is
# pinned to that revision (LUMINA_HAIKU_ABI) — see docs/engine.md.

if(NOT LUMINA_HAIKU_SOURCE_DIR OR NOT EXISTS "${LUMINA_HAIKU_SOURCE_DIR}/src/servers/app/Desktop.h")
	message(WARNING "Window Engine skipped: set LUMINA_HAIKU_SOURCE_DIR to a "
		"Haiku source tree (git clone https://github.com/haiku/haiku, "
		"checked out at your system's hrev).")
	return()
endif()

set(_HS "${LUMINA_HAIKU_SOURCE_DIR}")

# Determine the ABI tag: explicit > git describe of the source tree > uname.
if(NOT LUMINA_HAIKU_ABI)
	execute_process(COMMAND git -C "${_HS}" describe --tags --match "hrev*"
		--abbrev=0 OUTPUT_VARIABLE _ABI OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET)
	if(NOT _ABI AND LUMINA_ON_HAIKU)
		execute_process(COMMAND uname -v OUTPUT_VARIABLE _UNAME
			OUTPUT_STRIP_TRAILING_WHITESPACE)
		string(REGEX MATCH "^hrev[0-9]+" _ABI "${_UNAME}")
	endif()
	if(NOT _ABI)
		set(_ABI "unknown")
	endif()
	set(LUMINA_HAIKU_ABI "${_ABI}")
endif()
message(STATUS "Window Engine ABI: ${LUMINA_HAIKU_ABI}")

set(ENGINE_SOURCES
	LuminaDecorator.cpp
	LuminaEngine.cpp
	LuminaEngineLifecycle.cpp
	LuminaEngineOverlay.cpp
)

lumina_haiku_addon(LuminaEngine
	SOURCES ${ENGINE_SOURCES}
	LIBS lumina_core
)

target_compile_definitions(LuminaEngine PRIVATE
	LUMINA_ENGINE_ABI="${LUMINA_HAIKU_ABI}")
target_include_directories(LuminaEngine PRIVATE
	${PROJECT_SOURCE_DIR}/src/core
	${PROJECT_SOURCE_DIR}/src/liblumina)

# Same include semantics as Haiku's Jam build: app_server's own directories
# are quote-only (-iquote) so that <Window.h> still means the public
# Interface Kit header and not app_server's Window.h; private headers come
# after the system headers.
foreach(dir
		src/servers/app src/servers/app/decorator src/servers/app/drawing
		src/servers/app/drawing/Painter src/servers/app/font
		src/servers/app/stackandtile)
	target_compile_options(LuminaEngine PRIVATE "SHELL:-iquote ${_HS}/${dir}")
endforeach()
foreach(dir
		headers/private headers/private/app headers/private/shared
		headers/private/interface headers/private/graphics
		headers/private/input headers/private/storage headers/private/support
		headers/private/kernel headers/libs/agg)
	target_compile_options(LuminaEngine PRIVATE "SHELL:-idirafter ${_HS}/${dir}")
endforeach()
if(LUMINA_COMPILE_CHECK_ONLY)
	target_compile_options(LuminaEngine PRIVATE
		"SHELL:-idirafter ${HAIKU_CHECK_ROOT}/freetype2")
else()
	find_path(FREETYPE_INCLUDE ft2build.h
		PATHS /boot/system/develop/headers/freetype2
		PATH_SUFFIXES freetype2)
	target_compile_options(LuminaEngine PRIVATE
		"SHELL:-idirafter ${FREETYPE_INCLUDE}")
	# Decorator add-ons link against the app_server binary itself (as
	# Haiku's own decorators do with `<nogrist>app_server`).
	find_file(APP_SERVER_BINARY app_server PATHS /boot/system/servers
		NO_DEFAULT_PATH)
	target_link_libraries(LuminaEngine PRIVATE ${APP_SERVER_BINARY}
		${LUMINA_LIB_BE})
	install(TARGETS LuminaEngine
		LIBRARY DESTINATION add-ons/decorators)
endif()
