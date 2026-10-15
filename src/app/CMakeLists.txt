file(GLOB PX_APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
qt_add_executable(pxmodrim ${PX_APP_SOURCES})
target_link_libraries(pxmodrim PRIVATE pxui pxuiplugin)
