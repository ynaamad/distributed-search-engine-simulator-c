cmake_minimum_required(VERSION 3.20)
project(dse_sim C)

set(CMAKE_C_STANDARD 23)

# Sources
file(GLOB_RECURSE DSE_SOURCES src/*.c)

add_executable(dse_sim ${DSE_SOURCES})

target_include_directories(dse_sim PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(dse_sim PRIVATE m)

# Build configurations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(dse_sim PRIVATE -O3 -march=native -DNDEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(dse_sim PRIVATE -g -Wall -Wextra -Wpedantic)
endif()
