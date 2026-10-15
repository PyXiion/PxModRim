file(GLOB_RECURSE PX_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(pxcore STATIC ${PX_CORE_SOURCES})
target_include_directories(pxcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pxcore PUBLIC
    Qt6::Core Qt6::Concurrent Qt6::Network Qt6::Sql
    QCoro6::Core QCoro6::Network
    libzip::zip pugixml::pugixml spdlog::spdlog glaze::glaze)

if(PX_STEAM_LIBS)
    target_link_libraries(pxcore PUBLIC ${PX_STEAM_LIBS})
    target_compile_definitions(pxcore PUBLIC PX_HAS_STEAM=1)
endif()
