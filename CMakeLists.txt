cmake_minimum_required (VERSION 2.8)

set(lib_name MESA_field_stat2)

project (${lib_name})

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(Version)

set(CMAKE_MACOSX_RPATH 0)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -Wall)


#for ASAN
option(ENABLE_SANITIZE_ADDRESS "Enable AddressSanitizer" FALSE)
option(ENABLE_SANITIZE_THREAD "Enable ThreadSanitizer" FALSE)

if(ENABLE_SANITIZE_ADDRESS)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
elseif(ENABLE_SANITIZE_THREAD)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread -fno-omit-frame-pointer")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -fno-omit-frame-pointer")
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
set_target_properties(${lib_name}_shared PROPERTIES OUTPUT_NAME ${lib_name})

# static Library Output
add_library(${lib_name}_static STATIC ${SRC})
set_target_properties(${lib_name}_static PROPERTIES OUTPUT_NAME ${lib_name})

install(TARGETS ${lib_name}_shared ${lib_name}_static DESTINATION ${CMAKE_SOURCE_DIR}/lib)


file(GLOB DEMO
    "test/*.c"
    "test/*.cpp"
)

add_executable(demo ${DEMO})
target_link_libraries(demo  pthread ${lib_name}_static)
