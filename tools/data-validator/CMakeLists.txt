add_executable(rts_data_validator main.cpp)
target_include_directories(rts_data_validator PRIVATE "${CMAKE_SOURCE_DIR}/inc")
target_link_libraries(rts_data_validator PRIVATE rts_data)
target_compile_definitions(rts_data_validator PRIVATE
    RTS_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")

if (BUILD_TESTING)
    add_test(NAME rts_data_validator
             COMMAND rts_data_validator)
    set_tests_properties(rts_data_validator PROPERTIES LABELS "data;fast")
endif()
