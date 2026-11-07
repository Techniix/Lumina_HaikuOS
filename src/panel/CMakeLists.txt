lumina_haiku_executable(LuminaPanel
	SOURCES PanelApp.cpp PanelWindow.cpp PanelSkin.cpp Applet.cpp
		BuiltinApplets.cpp
	LIBS liblumina lumina_core ${LUMINA_LIB_BE} ${LUMINA_LIB_TRANSLATION}
	RDEF LuminaPanel.rdef
)
target_include_directories(LuminaPanel PRIVATE
	${PROJECT_SOURCE_DIR}/src/core ${PROJECT_SOURCE_DIR}/src/liblumina)
