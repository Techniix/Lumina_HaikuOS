add_executable(lumina_core_tests
	TestMain.cpp
	core/TestJsonValue.cpp
	core/TestConfig.cpp
	core/TestProfile.cpp
	core/TestRulesLog.cpp
	core/TestAnim.cpp
	core/TestFx.cpp
	core/TestPersonalities.cpp
	core/TestLifecycle.cpp
	core/TestOverlay.cpp
)
target_link_libraries(lumina_core_tests PRIVATE lumina_core)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
	target_link_libraries(lumina_core_tests PRIVATE stdc++fs)
endif()
add_test(NAME core COMMAND lumina_core_tests)

# Profile tests for the shipped personalities (data/personalities).
add_test(NAME personalities
	COMMAND lumina_core_tests personalities)
set_tests_properties(personalities PROPERTIES
	ENVIRONMENT "LUMINA_DATA_DIR=${PROJECT_SOURCE_DIR}/data")

add_executable(schema_doc ${PROJECT_SOURCE_DIR}/tools/schema_doc.cpp)
target_link_libraries(schema_doc PRIVATE lumina_core)
