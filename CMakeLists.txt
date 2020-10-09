cmake_minimum_required (VERSION 2.8)

set(lib_name MESA_field_stat2)

project (${lib_name})

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(Version)

set(CMAKE_MACOSX_RPATH 0)
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -Wall)
set(CMAKE_INSTALL_PREFIX /opt/MESA)

#for ASAN
set(ASAN_OPTION "OFF" CACHE STRING " set asan type chosen by the user, using OFF as default")
set_property(CACHE ASAN_OPTION  PROPERTY STRINGS OFF ADDRESS THREAD)
message(STATUS "ASAN_OPTION='${ASAN_OPTION}'")

if(ASAN_OPTION MATCHES "ADDRESS")
    set(CMAKE_C_FLAGS "${CMAKADDRESS} -g -DCMAKE_BUILD_TYPE=Debug -fsanitize=address -fno-omit-frame-pointer")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -DCMAKE_BUILD_TYPE=Debug -fsanitize=address -fno-omit-frame-pointer")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
elseif(ASAN_OPTION MATCHES "THREAD")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -DCMAKE_BUILD_TYPE=Debug -fsanitize=thread -fno-omit-frame-pointer")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -DCMAKE_BUILD_TYPE=Debug -fsanitize=thread -fno-omit-frame-pointer")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
endif()
# end of for ASAN

include_directories(${PROJECT_SOURCE_DIR}/inc/)

file(GLOB SRC
    "src/*.c"
    "src/*.cpp"
)

# Shared Library Output
add_library(${lib_name}_shared SHARED ${SRC})
set_target_properties(${lib_name}_shared PROPERTIES LINK_FLAGS
    "-Wl,--version-script=${PROJECT_SOURCE_DIR}/src/version.map")
if(DEFINED MESA_SHARED_INSTALL_DIR)
	set_target_properties(${lib_name}_shared PROPERTIES OUTPUT_NAME ${lib_name} LIBRARY_OUTPUT_DIRECTORY ${MESA_SHARED_INSTALL_DIR})
else()
	set_target_properties(${lib_name}_shared PROPERTIES OUTPUT_NAME ${lib_name})
endif()

# static Library Output
add_library(${lib_name}_static STATIC ${SRC})
set_target_properties(${lib_name}_static PROPERTIES OUTPUT_NAME ${lib_name})

install(TARGETS ${lib_name}_shared LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib COMPONENT LIBRARY)
install(TARGETS ${lib_name}_static LIBRARY ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib COMPONENT LIBRARY)
install(FILES inc/field_stat2.h   DESTINATION   ${CMAKE_INSTALL_PREFIX}/include/MESA COMPONENT HEADER)

include(Package)

file(GLOB DEMO
    "test/*.c"
    "test/*.cpp"
)

add_executable(demo ${DEMO})
target_link_libraries(demo  pthread ${lib_name}_static)
