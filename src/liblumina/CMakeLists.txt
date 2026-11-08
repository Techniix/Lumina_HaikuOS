# liblumina: Haiku-native shared library (IPC, private-API compat layer,
# paths, logging sinks, system appearance snapshot, OS interface).
set(LIBLUMINA_SOURCES
	lumina/haiku/PrivateApi.cpp
	lumina/haiku/Paths.cpp
	lumina/haiku/HaikuLog.cpp
	lumina/haiku/MessageCodec.cpp
	lumina/haiku/LuminaClient.cpp
	lumina/haiku/EngineLink.cpp
	lumina/haiku/SystemAppearance.cpp
	lumina/haiku/OSInterface.cpp
	lumina/haiku/ModeControl.cpp
	lumina/haiku/VisualEffects.cpp
	lumina/haiku/CursorThemes.cpp
)

if(LUMINA_COMPILE_CHECK_ONLY)
	add_library(liblumina OBJECT ${LIBLUMINA_SOURCES})
else()
	add_library(liblumina SHARED ${LIBLUMINA_SOURCES})
	target_link_libraries(liblumina PUBLIC lumina_core ${LUMINA_LIB_BE}
		translation)
	set_target_properties(liblumina PROPERTIES OUTPUT_NAME lumina VERSION ${PROJECT_VERSION}
		SOVERSION ${PROJECT_VERSION_MAJOR})
endif()
target_include_directories(liblumina PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	${PROJECT_SOURCE_DIR}/src/core)
