# Visual-effects research prototypes (see docs/vfx.md). EXPERIMENTAL:
# never installed by default, never linked into stable components.
add_library(fxlab STATIC common/FxHarness.cpp)
target_include_directories(fxlab PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	${PROJECT_SOURCE_DIR}/src/core ${PROJECT_SOURCE_DIR}/src/liblumina)
if(LUMINA_COMPILE_CHECK_ONLY)
	set_target_properties(fxlab PROPERTIES EXCLUDE_FROM_ALL OFF)
endif()

foreach(proto fx-alpha-panel fx-capture-glass fx-window-anim fx-snapshot-proxy)
	file(GLOB _src ${CMAKE_CURRENT_SOURCE_DIR}/${proto}/*.cpp)
	lumina_haiku_executable(${proto}
		SOURCES ${_src}
		LIBS fxlab liblumina lumina_core ${LUMINA_LIB_BE}
	)
	target_include_directories(${proto} PRIVATE
		${PROJECT_SOURCE_DIR}/src/core ${PROJECT_SOURCE_DIR}/src/liblumina)
endforeach()
