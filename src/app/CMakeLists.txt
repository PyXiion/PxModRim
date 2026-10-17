file(GLOB PX_APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
qt_add_executable(pxmodrim ${PX_APP_SOURCES})
target_link_libraries(pxmodrim PRIVATE pxui pxuiplugin)

include(GNUInstallDirs)

# The app reads the Workshop variant from workshop-variant.txt beside its executable
# (px::app::parseDisabledPlugins), so the build tree and the install get the file too.
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/workshop-variant.txt"
     CONTENT "${PX_WORKSHOP_VARIANT}\n")
add_custom_command(TARGET pxmodrim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/workshop-variant.txt" "$<TARGET_FILE_DIR:pxmodrim>")

if(APPLE)
    set_target_properties(pxmodrim PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME PxModRim
        MACOSX_BUNDLE_GUI_IDENTIFIER com.github.PyXiion.PxModRim
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE logo.icns)
    target_sources(pxmodrim PRIVATE ${CMAKE_SOURCE_DIR}/packaging/logo.icns)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/packaging/logo.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set(PX_APP_INSTALL_DIR pxmodrim.app/Contents/MacOS)
    install(TARGETS pxmodrim BUNDLE DESTINATION .)
else()
    set(PX_APP_INSTALL_DIR ${CMAKE_INSTALL_BINDIR})
    install(TARGETS pxmodrim RUNTIME DESTINATION ${PX_APP_INSTALL_DIR})
endif()

if(WIN32)
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/pxmodrim.rc"
         CONTENT "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/packaging/logo.ico\"\n")
    target_sources(pxmodrim PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/pxmodrim.rc")
endif()

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/workshop-variant.txt" DESTINATION ${PX_APP_INSTALL_DIR})

if(UNIX AND NOT APPLE)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/pxmodrim.desktop DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/src/ui/assets/logo.svg RENAME pxmodrim.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()
