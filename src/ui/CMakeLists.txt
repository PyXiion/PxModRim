# Every C++ source/header, every QML file and the embedded resources are globbed, so
# new UI files need no CMake edit. QML files must sit directly in this directory
# (the module root): Qt does not reliably register types from subfolders, and plain
# type names only resolve within one flat module.

file(GLOB_RECURSE PX_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB_RECURSE PX_UI_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
file(GLOB PX_UI_QML CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.qml ${CMAKE_CURRENT_SOURCE_DIR}/*.js)

# Resources are addressed relative to this directory: the stylesheet is
# :/qt/qml/PxModRim/theme/style.qss, logos are :/qt/qml/PxModRim/assets/*, and a
# plugin's injected script/style is :/qt/qml/PxModRim/plugins/<name>/inject.*.
file(GLOB_RECURSE PX_UI_RESOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/theme/*.qss
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/*
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins/*/inject.css
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins/*/inject.js)

# The Steam Workshop browser imports QtWebEngine. Without WebEngine it cannot be
# compiled as QML, so it ships as a plain resource and is loaded only when enabled.
set(PX_UI_WEB_QML SteamWorkshop.qml)
set(PX_UI_HAS_WEBENGINE OFF)
if(TARGET Qt6::WebEngineQuick)
    set(PX_UI_HAS_WEBENGINE ON)
endif()
if(NOT PX_UI_HAS_WEBENGINE)
    list(FILTER PX_UI_QML EXCLUDE REGEX "/${PX_UI_WEB_QML}$")
    list(APPEND PX_UI_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${PX_UI_WEB_QML})
endif()

list(TRANSFORM PX_UI_RESOURCES REPLACE "^${CMAKE_CURRENT_SOURCE_DIR}/" "")
list(TRANSFORM PX_UI_QML REPLACE "^${CMAKE_CURRENT_SOURCE_DIR}/" "")

qt_add_library(pxui STATIC)
qt_add_qml_module(pxui
    URI PxModRim
    VERSION 1.0
    QML_FILES ${PX_UI_QML}
    SOURCES ${PX_UI_SOURCES} ${PX_UI_HEADERS}
    RESOURCES ${PX_UI_RESOURCES}
)
target_include_directories(pxui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pxui PUBLIC
    pxcore
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickWidgets
    Qt6::Svg Qt6::Widgets
    QCoro6::Core QCoro6::Qml QCoro6::Quick)
if(PX_UI_HAS_WEBENGINE)
    target_link_libraries(pxui PUBLIC Qt6::WebEngineQuick)
    target_compile_definitions(pxui PUBLIC PX_HAS_WEBENGINE=1)
endif()
