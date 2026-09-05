cmake_minimum_required(VERSION 3.16)

find_package(GTest CONFIG QUIET)

if (NOT GTest_FOUND)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        URL_HASH SHA256=1F357C27CA988C3F7C6B4BF68A9395005AC6761F034046E9DDE0896E3ABA00E4
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(tvorin_tests
    AtomicFileTests.cpp
    BalanceModifierTests.cpp
    BalanceContractTests.cpp
    BattleUnitTests.cpp
    BattleLifecycleTests.cpp
    BattleSimulatorTests.cpp
    UnitAssignmentTests.cpp
    DefenseComponentsTests.cpp
    GarrisonServiceTests.cpp
    RaidDamageTests.cpp
    WarfareViewsTests.cpp
    BuildingConfigTests.cpp
    BuildingPresentationTests.cpp
    BuildingDomainTests.cpp
    BuildingSalvageTests.cpp
    BuildingPlacementTests.cpp
    EconomyExpansionTests.cpp
    FogOfWarTests.cpp
    FaultInjectingGameTransportTests.cpp
    GameCommandTests.cpp
    GameSnapshotTests.cpp
    CampaignPersistenceTests.cpp
    ProvinceCommandRoutingTests.cpp
    ScoutExpeditionTests.cpp
    MapGeneratorTests.cpp
    NetworkProtocolTests.cpp
    PlayerEconomyTests.cpp
    PeacefulWorldTests.cpp
    PersistenceValidationTests.cpp
    RendererAnimationTests.cpp
    RaylibResourceTests.cpp
    ResourceBufferTests.cpp
    RtsDataFileTests.cpp
    RoadNetworkTests.cpp
    RoadPriorityArbitrationTests.cpp
    SceneUtilsTests.cpp
    GameSessionPauseTests.cpp
    GameplayClockTests.cpp
    LogTests.cpp
    SimulationStateTests.cpp
    SnapshotTransferTests.cpp
    TcpGameTransportTests.cpp
    TileMapDomainTests.cpp
    TechnologyTests.cpp
    SimulationPerfTests.cpp
    UiTextTests.cpp
    UpgradeLineRasterizerTests.cpp
    UpgradeGestureTargetsTests.cpp
    BuildInteractionStateTests.cpp
    WorldLightingTests.cpp
    GlobalMapTests.cpp
    GlobalMapGeneratorV2Tests.cpp
    ColonizationTests.cpp
    ProvinceDefinitionTests.cpp
    ProvinceConnectionTests.cpp
    TradeTests.cpp
    WorldJourneyTests.cpp
    JourneyTimingTests.cpp
    WorldEventSystemTests.cpp
    WorldEventDefinitionTests.cpp
    ProvinceDomainTests.cpp
    StrategicHudLayoutTests.cpp
    KeyBindingsTests.cpp
)

target_link_libraries(tvorin_tests
    PRIVATE
        game
        GTest::gtest_main
        GTest::gmock
        Threads::Threads
)

# winmm is not linked here: WinmmStubs.cpp (in game) provides timeBeginPeriod/timeEndPeriod
# via dynamic loading, avoiding the PlaySound symbol conflict with raylib.

target_include_directories(tvorin_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/inc
)

if (ENABLE_BUILD_ACCELERATION)
    target_precompile_headers(tvorin_tests PRIVATE
        <gmock/gmock.h>
        <gtest/gtest.h>
        <algorithm>
        <array>
        <chrono>
        <cstdint>
        <filesystem>
        <map>
        <memory>
        <optional>
        <set>
        <string>
        <utility>
        <vector>
    )

    if (MSVC)
        target_compile_options(tvorin_tests PRIVATE /MP)
    endif()
endif()

if (ENABLE_COVERAGE AND MSVC)
    target_compile_options(tvorin_tests PRIVATE /Od /Zi)
    target_link_options(tvorin_tests PRIVATE /DEBUG)
endif()

add_test(NAME tvorin_tests COMMAND tvorin_tests)
set_tests_properties(tvorin_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    LABELS "fast;integration;network;data"
)

if (ENABLE_COVERAGE AND MSVC)
    find_program(OPENCPPCOVERAGE_EXECUTABLE OpenCppCoverage OpenCppCoverage.exe)
    if (OPENCPPCOVERAGE_EXECUTABLE)
        file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/src" COVERAGE_SRC_DIR)
        file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/inc" COVERAGE_INC_DIR)
        file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/tests" COVERAGE_TESTS_DIR)
        file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/coverage" COVERAGE_HTML_DIR)
        file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/coverage.xml" COVERAGE_XML_FILE)
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
            COMMAND ${OPENCPPCOVERAGE_EXECUTABLE}
                    --sources ${COVERAGE_SRC_DIR}
                    --sources ${COVERAGE_INC_DIR}
                    --excluded_sources ${COVERAGE_TESTS_DIR}
                    --export_type html:${COVERAGE_HTML_DIR}
                    --export_type cobertura:${COVERAGE_XML_FILE}
                    -- $<TARGET_FILE:tvorin_tests>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            DEPENDS tvorin_tests
            COMMENT "Running fast unit tests and generating OpenCppCoverage report"
        )
    else()
        message(WARNING "OpenCppCoverage not found. Install it or pass -DOPENCPPCOVERAGE_EXECUTABLE=path/to/OpenCppCoverage.exe")
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E echo "OpenCppCoverage not found. Install it or configure with -DOPENCPPCOVERAGE_EXECUTABLE=path/to/OpenCppCoverage.exe"
            COMMAND ${CMAKE_COMMAND} -E false
            COMMENT "Coverage requires OpenCppCoverage on Windows/MSVC"
        )
    endif()
elseif (ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    find_program(GCOVR_EXECUTABLE gcovr)
    if (GCOVR_EXECUTABLE)
        add_custom_target(coverage
            COMMAND $<TARGET_FILE:tvorin_tests>
            COMMAND ${GCOVR_EXECUTABLE}
                    --root ${CMAKE_SOURCE_DIR}
                    --filter ${CMAKE_SOURCE_DIR}/src
                    --filter ${CMAKE_SOURCE_DIR}/inc
                    --exclude ${CMAKE_SOURCE_DIR}/tests
                    --print-summary
                    --html-details ${CMAKE_BINARY_DIR}/coverage.html
                    --xml ${CMAKE_BINARY_DIR}/coverage.xml
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            DEPENDS tvorin_tests
            COMMENT "Running fast unit tests and generating gcov coverage report"
        )
    else()
        message(WARNING "gcovr not found.")
    endif()
endif()
