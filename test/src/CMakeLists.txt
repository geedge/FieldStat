cmake_minimum_required(VERSION 2.8)

include_directories(${PROJECT_SOURCE_DIR}/inc/)
include_directories(${PROJECT_SOURCE_DIR}/src/)
add_definitions(-std=c++11)
#add_executable(gtest_rule ${PROJECT_SOURCE_DIR}/src/tsg_rule.cpp ${PROJECT_SOURCE_DIR}/src/tsg_bridge.cpp ${PROJECT_SOURCE_DIR}/src/tsg_leaky_bucket.cpp gtest_common.cpp gtest_rule.cpp)
#target_link_libraries(gtest_rule gtest-static ctemplate-static cjson MESA_prof_load MESA_handle_logger MESA_jump_layer MESA_field_stat2 maatframe)

add_executable(gtest_fieldstat_interface ${SRC} gtest_fieldstat_interface.cpp)
target_link_libraries(gtest_fieldstat_interface gtest-static)

add_executable(gtest_fieldstat_output ${SRC} gtest_fieldstat_output.cpp)
target_link_libraries(gtest_fieldstat_output gtest-static)

add_executable(gtest_dynamic_fieldstat ${SRC} gtest_dynamic_fieldstat.cpp)
target_link_libraries(gtest_dynamic_fieldstat gtest-static)

add_executable(gtest_dynamic_fieldstat_output ${SRC} gtest_dynamic_fieldstat_output.cpp)
target_link_libraries(gtest_dynamic_fieldstat_output gtest-static)

add_executable(gtest_fieldstat_output_file_instance ${SRC} gtest_fieldstat_output_file_instance.cpp)
target_link_libraries(gtest_fieldstat_output_file_instance gtest-static)