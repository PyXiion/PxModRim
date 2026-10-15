include(Catch)

file(GLOB_RECURSE PX_CORE_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
add_executable(pxcore_tests ${PX_CORE_TESTS})
target_link_libraries(pxcore_tests PRIVATE pxcore Catch2::Catch2WithMain)
catch_discover_tests(pxcore_tests)
target_compile_definitions(pxcore_tests PRIVATE PX_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

file(GLOB_RECURSE PX_UI_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp)
add_executable(pxui_tests ${PX_UI_TESTS})
target_link_libraries(pxui_tests PRIVATE pxui pxuiplugin Qt6::Test Catch2::Catch2)
catch_discover_tests(pxui_tests)
