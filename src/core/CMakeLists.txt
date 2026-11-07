# lumina_core: portable C++17, no Haiku API. Linked into every component
# (including the app_server add-on) and unit-tested on any host.
add_library(lumina_core STATIC
	lumina/Value.cpp
	lumina/Json.cpp
	lumina/Log.cpp
	lumina/ConfigSchema.cpp
	lumina/ConfigSchemaData.cpp
	lumina/ConfigStore.cpp
	lumina/Profile.cpp
	lumina/Rules.cpp
	lumina/anim/Easing.cpp
	lumina/anim/Timeline.cpp
	lumina/fx/Image.cpp
	lumina/fx/Glass.cpp
	lumina/fx/Strategy.cpp
)
target_include_directories(lumina_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(lumina_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
