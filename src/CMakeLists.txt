
aux_source_directory(${PROJECT_SOURCE_DIR}/vendors LIB_SOURCE_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/vendors/minheap LIB_SOURCE_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/vendors/xxhash LIB_SOURCE_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/vendors/mpack LIB_SOURCE_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/vendors/cjson LIB_SOURCE_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/vendors/hdr_histogram LIB_SOURCE_FILES)

aux_source_directory(${PROJECT_SOURCE_DIR}/src SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/metrics SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/output SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/utils SRC_LIST)

include_directories(${PROJECT_SOURCE_DIR}/vendors)
include_directories(${PROJECT_SOURCE_DIR}/src/metrics)
include_directories(${PROJECT_SOURCE_DIR}/src/export)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/include)

add_library(swarmsketch-static STATIC ${SRC_LIST} ${LIB_SOURCE_FILES})
set_target_properties(swarmsketch-static PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(swarmsketch-static PROPERTIES OUTPUT_NAME swarmsketch)
set_target_properties(swarmsketch-static PROPERTIES CLEAN_DIRECT_OUTPUT 1)

add_library(swarmsketch-shared SHARED ${SRC_LIST} ${LIB_SOURCE_FILES})
set_target_properties(swarmsketch-shared PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(swarmsketch-shared PROPERTIES OUTPUT_NAME swarmsketch)
set_target_properties(swarmsketch-shared PROPERTIES CLEAN_DIRECT_OUTPUT 1)



set(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR})
install(FILES ${PROJECT_SOURCE_DIR}/src/swarm_sketch.h DESTINATION ${PROJECT_SOURCE_DIR}/include COMPONENT HEADER)
install(TARGETS swarmsketch-shared LIBRARY DESTINATION ${PROJECT_SOURCE_DIR}/libs/ COMPONENT library)
