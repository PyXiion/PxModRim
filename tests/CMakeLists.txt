include(Catch)

add_executable(pxcore_tests core/test_version.cpp)
target_link_libraries(pxcore_tests PRIVATE pxcore Catch2::Catch2WithMain)
catch_discover_tests(pxcore_tests)
