cmake_policy(SET CMP0048 NEW)

project(HammingTree LANGUAGES CXX CUDA VERSION 3.0)

cmake_minimum_required(VERSION 3.0)

add_subdirectory(../libShapeDescriptor ${CMAKE_CURRENT_BINARY_DIR}/libShapeDescriptor)

include_directories (
        src/
        lib/glm/
        lib/nvidia-samples-common/
        lib/lodepng/
        lib/arrrgh/
        lib/json/
        lib/fast-lzma2/src/
        lib/
        ../libShapeDescriptor/src
        ${CUDA_INCLUDE_DIRS}
)


file (GLOB_RECURSE LIBRARY_HEADERS             src/*.hpp
        src/*.h
        src/*.cuh)
file (GLOB_RECURSE LIBRARY_KERNELS             src/*.cu
        lib/nvidia-samples-common/*.cu)
file (GLOB_RECURSE LIBRARY_SOURCES             src/*.cpp
        src/*.cxx
        src/*.cc
        src/*.c
        lib/lodepng/lodepng.cpp)

file (GLOB_RECURSE TEST_SOURCES                tests/*.cpp)
file (GLOB_RECURSE RENDER_TOOL_SOURCES         tools/imagerenderer/*.cpp)
file (GLOB_RECURSE INDEX_BUILD_TOOL_SOURCES    tools/indexbuilder/*.cpp)
file (GLOB_RECURSE INDEX_QUERY_TOOL_SOURCES    tools/indexqueryer/*.cpp)
file (GLOB_RECURSE INDEX_BENCHMARK_TOOL_SOURCES tools/indexBenchmark/*.cpp)
file (GLOB_RECURSE QUICCI_DUMPER_TOOL_SOURCES  tools/quiccidumper/*.cpp)
file (GLOB_RECURSE COMPRESSOR_TOOL_SOURCES     tools/compressor/*.cpp)

add_library(${PROJECT_NAME} STATIC ${LIBRARY_SOURCES} ${LIBRARY_HEADERS} ${LIBRARY_KERNELS})
target_link_libraries(${PROJECT_NAME} stdc++fs fast-lzma2)
target_include_directories(${PROJECT_NAME} PUBLIC src/)

add_executable(buildindex ${INDEX_BUILD_TOOL_SOURCES})
target_link_libraries(buildindex ShapeDescriptor HammingTree cudadevrt)

add_executable(benchmarkindex ${INDEX_BENCHMARK_TOOL_SOURCES})
target_link_libraries(benchmarkindex ShapeDescriptor HammingTree cudadevrt)

add_executable(queryindex ${INDEX_QUERY_TOOL_SOURCES})
target_link_libraries(queryindex ShapeDescriptor HammingTree cudadevrt)

add_executable(compressor ${COMPRESSOR_TOOL_SOURCES})
target_link_libraries(compressor ShapeDescriptor HammingTree cudadevrt)