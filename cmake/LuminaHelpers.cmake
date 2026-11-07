# Helpers that let every Haiku target be built natively on Haiku, or
# compiled (not linked) from a Linux host with tools/haiku-compile-check.sh.

# lumina_haiku_executable(<name> SOURCES ... LIBS ... [RDEF file])
function(lumina_haiku_executable name)
	cmake_parse_arguments(ARG "" "RDEF" "SOURCES;LIBS" ${ARGN})
	if(LUMINA_COMPILE_CHECK_ONLY)
		add_library(${name} OBJECT ${ARG_SOURCES})
		foreach(lib ${ARG_LIBS})
			if(TARGET ${lib})
				target_link_libraries(${name} PRIVATE ${lib})
			endif()
		endforeach()
		return()
	endif()
	add_executable(${name} ${ARG_SOURCES})
	target_link_libraries(${name} PRIVATE ${ARG_LIBS})
	if(ARG_RDEF)
		lumina_add_resources(${name} ${ARG_RDEF})
	endif()
endfunction()

# lumina_haiku_addon(<name> SOURCES ... LIBS ...)  – a shared object add-on
function(lumina_haiku_addon name)
	cmake_parse_arguments(ARG "" "RDEF" "SOURCES;LIBS" ${ARGN})
	if(LUMINA_COMPILE_CHECK_ONLY)
		add_library(${name} OBJECT ${ARG_SOURCES})
		foreach(lib ${ARG_LIBS})
			if(TARGET ${lib})
				target_link_libraries(${name} PRIVATE ${lib})
			endif()
		endforeach()
		return()
	endif()
	add_library(${name} MODULE ${ARG_SOURCES})
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
	target_link_libraries(${name} PRIVATE ${ARG_LIBS})
	if(ARG_RDEF)
		lumina_add_resources(${name} ${ARG_RDEF})
	endif()
endfunction()

# Compiles an .rdef with `rc`, attaches it with `xres`, then `mimeset`s the
# binary so Tracker/registrar see its signature. Only on a real Haiku host.
function(lumina_add_resources target rdef)
	if(NOT LUMINA_ON_HAIKU)
		return()
	endif()
	find_program(LUMINA_RC rc)
	find_program(LUMINA_XRES xres)
	find_program(LUMINA_MIMESET mimeset)
	if(NOT LUMINA_RC OR NOT LUMINA_XRES)
		message(WARNING "rc/xres not found: ${target} built without resources")
		return()
	endif()
	get_filename_component(base ${rdef} NAME_WE)
	set(rsrc "${CMAKE_CURRENT_BINARY_DIR}/${base}.rsrc")
	add_custom_command(OUTPUT ${rsrc}
		COMMAND ${LUMINA_RC} -o ${rsrc} ${CMAKE_CURRENT_SOURCE_DIR}/${rdef}
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${rdef}
		COMMENT "Compiling resources ${rdef}")
	add_custom_target(${target}_rsrc DEPENDS ${rsrc})
	add_dependencies(${target} ${target}_rsrc)
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND ${LUMINA_XRES} -o $<TARGET_FILE:${target}> ${rsrc}
		COMMAND ${LUMINA_MIMESET} -f $<TARGET_FILE:${target}>
		COMMENT "Attaching resources to ${target}")
endfunction()

# Haiku system libraries, as plain names (libbe.so etc. live on the default
# library path on Haiku).
set(LUMINA_LIB_BE be)
set(LUMINA_LIB_TRACKER tracker)
set(LUMINA_LIB_MEDIA media)
set(LUMINA_LIB_TRANSLATION translation)
set(LUMINA_LIB_LOCALESTUB localestub)
