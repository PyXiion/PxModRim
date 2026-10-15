qt_add_library(pxui STATIC)
qt_add_qml_module(pxui
    URI PxModRim
    VERSION 1.0
    QML_FILES Main.qml
    SOURCES placeholder.cpp
)
target_include_directories(pxui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pxui PUBLIC pxcore Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Svg Qt6::Widgets QCoro6::Qml QCoro6::Quick)
