lumina_haiku_executable(lumina_server
	SOURCES ServerApp.cpp WindowRegistry.cpp EngineClock.cpp
	LIBS liblumina lumina_core ${LUMINA_LIB_BE}
	RDEF lumina_server.rdef
)
target_include_directories(lumina_server PRIVATE
	${PROJECT_SOURCE_DIR}/src/core ${PROJECT_SOURCE_DIR}/src/liblumina)
