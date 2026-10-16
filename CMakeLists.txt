cmake_minimum_required(VERSION 3.24)
project(PxModRim VERSION 0.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

option(PX_BUILD_TESTS "Build tests" ON)
option(PX_BUILD_UI "Build the Qt Quick UI and the executable" ON)
set(PX_WORKSHOP_VARIANT "SteamWorkshop" CACHE STRING
    "NoWorkshop | SteamWorkshop | NativeWorkshop")
set_property(CACHE PX_WORKSHOP_VARIANT PROPERTY STRINGS
    NoWorkshop SteamWorkshop NativeWorkshop)

find_program(PX_CCACHE ccache)
if(PX_CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${PX_CCACHE}")
    message(STATUS "ccache: ${PX_CCACHE}")
endif()

set(CPM_USE_LOCAL_PACKAGES ON)
set(CPM_SOURCE_CACHE "$ENV{HOME}/.cache/CPM" CACHE PATH "")
include(cmake/CPM.cmake)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Concurrent Network Sql Gui Quick QuickControls2 QuickWidgets Svg Widgets Test)
qt_standard_project_setup(REQUIRES 6.8)
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

if(NOT PX_WORKSHOP_VARIANT STREQUAL "NativeWorkshop" AND NOT PX_WORKSHOP_VARIANT STREQUAL "NoWorkshop")
    find_package(Qt6 6.8 REQUIRED COMPONENTS WebEngineQuick)
endif()
find_package(QCoro6 REQUIRED COMPONENTS Core Network Qml Quick)
qcoro_enable_coroutines()

find_package(pugixml REQUIRED)
find_package(libzip REQUIRED)
find_package(spdlog REQUIRED)
CPMAddPackage("gh:stephenberry/glaze@5.5.4")
if(PX_BUILD_TESTS)
    CPMAddPackage("gh:catchorg/Catch2@3.8.1")
    list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
endif()

# NoWorkshop ships without Steam downloads, so PxSteamDL is not fetched or linked.
if(NOT PX_WORKSHOP_VARIANT STREQUAL "NoWorkshop")
    include(cmake/Steam.cmake OPTIONAL)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

add_subdirectory(src/core)
if(PX_BUILD_UI)
    add_subdirectory(src/ui)
    add_subdirectory(src/app)
endif()

if(PX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
