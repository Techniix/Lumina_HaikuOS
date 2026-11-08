# Install layout = Haiku package layout (CMAKE_INSTALL_PREFIX is the package
# root, e.g. /boot/home/config/non-packaged during development, or the
# staging dir of the HaikuPorts recipe).

set(LUMINA_APPS_DIR "apps/Lumina")
install(TARGETS liblumina LIBRARY DESTINATION lib)
install(TARGETS lumina_server LuminaPanel RUNTIME DESTINATION ${LUMINA_APPS_DIR})
install(TARGETS lumina RUNTIME DESTINATION bin)
install(PROGRAMS $<TARGET_FILE:LuminaMode> DESTINATION preferences
	RENAME "Lumina Mode")
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/personalities
	DESTINATION data/Lumina)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/cursors
	DESTINATION data/Lumina)

if(NOT LUMINA_SERVER_PATH)
	set(LUMINA_SERVER_PATH "${CMAKE_INSTALL_PREFIX}/${LUMINA_APPS_DIR}/lumina_server")
endif()
configure_file(${PROJECT_SOURCE_DIR}/data/user_launch/lumina.in
	${CMAKE_BINARY_DIR}/user_launch/lumina @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/user_launch/lumina DESTINATION data/user_launch)
