# cmake_minimum_required(VERSION 2.8.8)

# project(fieldstat_ctest)

# add_test(NAME COPY_CREATE_DIR COMMAND sh -c "mkdir -p ${CMAKE_BINARY_DIR}/testing/ ")

# add_test(NAME COPY_GTEST_JSON_EXPORTER_BINARY COMMAND sh -c "cp ${CMAKE_BINARY_DIR}/test/test_exporter_json ${CMAKE_BINARY_DIR}/testing/")
# add_test(NAME COPY_GTEST_MERGE_BINARY COMMAND sh -c "cp ${CMAKE_BINARY_DIR}/test/test_merge ${CMAKE_BINARY_DIR}/testing/")
# add_test(NAME COPY_GTEST_METRIC_COUNTER_BINARY COMMAND sh -c "cp ${CMAKE_BINARY_DIR}/test/test_metric_counter ${CMAKE_BINARY_DIR}/testing/")
# add_test(NAME COPY_GTEST_METRIC_HISTOGRAM_BINARY COMMAND sh -c "cp ${CMAKE_BINARY_DIR}/test/test_metric_histogram ${CMAKE_BINARY_DIR}/testing/")
# add_test(NAME COPY_GTEST_METRIC_HLL_BINARY COMMAND sh -c "cp ${CMAKE_BINARY_DIR}/test/test_metric_hll ${CMAKE_BINARY_DIR}/testing/")
# add_test(NAME COPY_GTEST_RESET_AND_DELETE_CUBE_BINARY COMMAND sh -c "cp ${CMAKE_BINARY_DIR}/test/test_reset_and_delete_cube ${CMAKE_BINARY_DIR}/testing/")
# add_test(NAME COPY_GTEST_SERIALIZE_BINARY COMMAND sh -c "cp ${CMAKE_BINARY_DIR}/test/test_serialize ${CMAKE_BINARY_DIR}/testing/")

# set(GTEST_RUN_DIR ${CMAKE_BINARY_DIR}/testing)
# add_test(NAME GTEST_JSON_EXPORTER COMMAND test_exporter_json WORKING_DIRECTORY ${GTEST_RUN_DIR})
# add_test(NAME GTEST_MERGE COMMAND test_merge WORKING_DIRECTORY ${GTEST_RUN_DIR})
# add_test(NAME GTEST_METRIC_COUNTER COMMAND test_metric_counter WORKING_DIRECTORY ${GTEST_RUN_DIR})
# add_test(NAME GTEST_METRIC_HISTOGRAM COMMAND test_metric_histogram WORKING_DIRECTORY ${GTEST_RUN_DIR})
# add_test(NAME GTEST_METRIC_HLL COMMAND test_metric_hll WORKING_DIRECTORY ${GTEST_RUN_DIR})
# add_test(NAME GTEST_RESET_AND_DELETE_CUBE COMMAND test_reset_and_delete_cube WORKING_DIRECTORY ${GTEST_RUN_DIR})
# add_test(NAME GTEST_SERIALIZE COMMAND test_serialize WORKING_DIRECTORY ${GTEST_RUN_DIR})
