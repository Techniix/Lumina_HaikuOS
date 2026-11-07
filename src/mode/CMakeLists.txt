lumina_haiku_executable(LuminaMode
	SOURCES ModeApp.cpp
	LIBS liblumina lumina_core ${LUMINA_LIB_BE}
	RDEF LuminaMode.rdef
)
target_include_directories(LuminaMode PRIVATE
	${PROJECT_SOURCE_DIR}/src/core ${PROJECT_SOURCE_DIR}/src/liblumina)
