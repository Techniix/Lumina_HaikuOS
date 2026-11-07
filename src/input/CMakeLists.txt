lumina_haiku_addon(LuminaInput
	SOURCES LuminaInputFilter.cpp
	LIBS ${LUMINA_LIB_BE}
)
if(NOT LUMINA_COMPILE_CHECK_ONLY)
	install(TARGETS LuminaInput LIBRARY DESTINATION add-ons/input_server/filters)
endif()
