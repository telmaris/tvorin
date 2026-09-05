cmake_minimum_required(VERSION 3.16)

# Version: single source of truth is the repository-root VERSION file.
# Reconfigure automatically whenever it changes (e.g. after a build-time patch bump).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TVORIN_VERSION_RAW LIMIT_COUNT 1)
string(STRIP "${TVORIN_VERSION_RAW}" TVORIN_VERSION)
if (NOT TVORIN_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION file malformed: '${TVORIN_VERSION}' (expected MAJOR.MINOR.PATCH)")
endif()

project(Tvorin VERSION ${TVORIN_VERSION} LANGUAGES CXX)

# Generate a header carrying the version into the binary (written to the build tree).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in"
    "${CMAKE_BINARY_DIR}/generated/Version.h"
    @ONLY)
include_directories("${CMAKE_BINARY_DIR}/generated")
message(STATUS "Tvorin version: ${TVORIN_VERSION}")

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_COVERAGE "Enable coverage reporting for unit tests" OFF)
option(BUILD_TESTING "Build unit tests" OFF)
option(BUILD_TOOLS "Build developer data editors" ON)
include(CTest)

if (NOT MSVC)
    add_compile_options(-Wno-narrowing)
endif()

# When vcpkg provides statically-linked dependencies (x64-windows-static triplet),
# our own code must also use the static CRT to avoid MSVC "defaultlib conflicts".
if (MSVC AND DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "static")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(raylib 5.0 QUIET)
find_package(Threads REQUIRED)

if (NOT raylib_FOUND)
    if (raylib_INCLUDE_DIR AND raylib_LIBRARY)
        # Pre-built raylib supplied via -Draylib_INCLUDE_DIR / -Draylib_LIBRARY (local dev).
        add_library(raylib UNKNOWN IMPORTED)
        set_target_properties(raylib PROPERTIES
            IMPORTED_LOCATION "${raylib_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${raylib_INCLUDE_DIR}")
    else()
        # No pre-built supplied — fetch and build raylib 5.0 from source (CI / first-time setup).
        message(STATUS "raylib not found locally; fetching raylib 5.0 via FetchContent")
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG        5.0
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(raylib)
    endif()
endif()

# Force STATIC regardless of BUILD_SHARED_LIBS (vcpkg x64-windows sets it ON).
add_library(game STATIC "")

set(raygui_INCLUDE_DIR "" CACHE PATH "Path to directory containing raygui.h")
if (NOT raygui_INCLUDE_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/deps/raygui/raygui.h")
    set(raygui_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/deps/raygui")
endif()
if (raygui_INCLUDE_DIR)
    target_include_directories(game PUBLIC "${raygui_INCLUDE_DIR}")
endif()

include_directories(inc)

# Shared production code used by the game and the asset editors. Keeping these
# translation units in one target prevents parser and UI behavior from drifting
# between the runtime and the tools.
add_library(rts_data STATIC
    src/core/CheckedStateReader.cpp
    src/platform/AtomicFile.cpp
    src/data/RtsDataFile.cpp
    src/data/Resource.cpp
    src/data/Equipment.cpp
    src/data/TextureConfig.cpp
    src/world/ProvinceDefinition.cpp
    src/world/ColonizationDefinition.cpp
    src/world/ProvinceConnection.cpp
    src/world/WorldEventDefinition.cpp
    src/warfare/BattleSimulator.cpp)
target_include_directories(rts_data PUBLIC "${CMAKE_SOURCE_DIR}/inc")

add_library(rts_ui_common STATIC
    src/economy/BalanceStatDisplay.cpp
    src/ui/ControlIcons.cpp
    src/ui/KeyBindings.cpp
    src/ui/UiText.cpp
    src/ui/UiWidgets.cpp)
target_include_directories(rts_ui_common PUBLIC "${CMAKE_SOURCE_DIR}/inc")
target_link_libraries(rts_ui_common PUBLIC raylib)

add_subdirectory(src)

option(ENABLE_BUILD_ACCELERATION "Enable parallel MSVC compilation and precompiled standard-library headers" ON)
if (ENABLE_BUILD_ACCELERATION)
    target_precompile_headers(game PRIVATE
        <algorithm>
        <array>
        <atomic>
        <chrono>
        <cstdint>
        <deque>
        <filesystem>
        <fstream>
        <functional>
        <iomanip>
        <iostream>
        <limits>
        <map>
        <memory>
        <mutex>
        <optional>
        <queue>
        <random>
        <set>
        <sstream>
        <string>
        <thread>
        <type_traits>
        <utility>
        <vector>
    )

    if (MSVC)
        target_compile_options(game PRIVATE /MP)
    endif()
endif()

if (ENABLE_COVERAGE)
    if (MSVC)
        target_compile_options(game PUBLIC /Od /Zi)
        target_link_options(game PUBLIC /DEBUG)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(game PUBLIC -O0 -g --coverage)
        target_link_options(game PUBLIC --coverage)
    else()
        message(WARNING "ENABLE_COVERAGE is supported only with MSVC or GCC/Clang gcov-compatible compilers.")
    endif()
endif()

add_executable(tvorin src/main.cpp)
target_link_libraries(game PUBLIC raylib rts_data rts_ui_common)
target_link_libraries(tvorin game Threads::Threads)

if (WIN32)
    target_link_libraries(game PUBLIC ws2_32)
    # winmm is NOT linked here: winmm.lib exports a "PlaySound" symbol that collides with
    # raylib's PlaySound(Sound). WinmmStubs.cpp provides timeBeginPeriod/timeEndPeriod
    # (the only winmm symbols raylib needs) via dynamic LoadLibrary, avoiding the conflict.
    if (MSVC)
        target_link_options(tvorin PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    endif()
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(tvorin dl m)
endif()

add_custom_command(TARGET tvorin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:tvorin>/assets
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:tvorin>/saves
)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()

if (BUILD_TOOLS)
    add_subdirectory(tools/data-validator)
    add_subdirectory(tools/tech-tree-editor)
    add_subdirectory(tools/texture-editor)
endif()
