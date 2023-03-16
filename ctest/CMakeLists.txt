cmake_minimum_required (VERSION 2.8)

project(fieldstat_ctest)

add_test(NAME COPY_CREATE_DIR COMMAND sh -c "mkdir -p ${CMAKE_BINARY_DIR}/testing/ ")

add_test(NAME COPY_GTEST_BINARY COMMAND sh -c "cp ${CMAKE_BINARY_DIR}/test/src/gtest_fieldstat ${CMAKE_BINARY_DIR}/testing/")
add_test(NAME COPY_GTEST_DYNAMIC_BINARY COMMAND sh -c "cp ${CMAKE_BINARY_DIR}/test/src/gtest_dynamic_fieldstat ${CMAKE_BINARY_DIR}/testing/")

set(GTEST_RUN_DIR ${CMAKE_BINARY_DIR}/testing)
#add_test(NAME GTEST_RULE COMMAND gtest_rule WORKING_DIRECTORY ${GTEST_RUN_DIR})
add_test(NAME GTEST_FIELDSTAT COMMAND gtest_fieldstat WORKING_DIRECTORY ${GTEST_RUN_DIR})
add_test(NAME GTEST_DYNAMIC_FIELDSTAT COMMAND gtest_dynamic_fieldstat WORKING_DIRECTORY ${GTEST_RUN_DIR})

