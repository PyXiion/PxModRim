add_library(pxcore STATIC)
target_sources(pxcore PRIVATE version.cpp)
target_include_directories(pxcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pxcore PUBLIC
    Qt6::Core Qt6::Network Qt6::Sql
    QCoro6::Core QCoro6::Network
    pugixml::pugixml spdlog::spdlog glaze::glaze)
