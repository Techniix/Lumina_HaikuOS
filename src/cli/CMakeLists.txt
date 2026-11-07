lumina_haiku_executable(lumina
	SOURCES LuminaCli.cpp
	LIBS liblumina lumina_core ${LUMINA_LIB_BE}
	RDEF lumina_cli.rdef
)
target_include_directories(lumina PRIVATE
	${PROJECT_SOURCE_DIR}/src/core ${PROJECT_SOURCE_DIR}/src/liblumina)
