cmake_minimum_required(VERSION 3.15)
project(image_to_jpeg CXX)

# C++17 for all projects and tests
set(CMAKE_CXX_STANDARD 17)

# The tools binary
add_executable(image_to_jpeg src/main.cpp src/toojpeg_17.cpp)
target_include_directories(image_to_jpeg PUBLIC src)
target_compile_options(image_to_jpeg PRIVATE -Wall -Wextra)

# The benchmark binary
add_executable(benchmark src/toojpeg_17.cpp src/benchmark/main.cpp src/benchmark/toojpeg.cpp)
target_include_directories(benchmark PUBLIC src src/benchmark)
target_compile_options(benchmark PRIVATE -Wall -Wextra -O3)

if (UNIX)
    # C++17 support is not complete/self-contained on linux g++: tbb need to be linked for parallel std algorithms
    target_link_libraries(image_to_jpeg tbb)
endif()

# Optionally link openSSL for https URLs (and the benchmark)
find_package(PkgConfig)
pkg_search_module(OPENSSL REQUIRED openssl)

if( OPENSSL_FOUND )
    target_include_directories(image_to_jpeg PRIVATE ${OPENSSL_INCLUDE_DIRS})
    message(STATUS "Using OpenSSL ${OPENSSL_VERSION}")
    target_link_libraries(image_to_jpeg ${OPENSSL_LIBRARIES})
    target_compile_definitions(image_to_jpeg PRIVATE -DWITH_HTTPS)

    target_link_libraries(benchmark ${OPENSSL_LIBRARIES})
    target_compile_definitions(benchmark PRIVATE -DWITH_HTTPS)
endif()

enable_testing()
add_subdirectory(tests)

find_package(Doxygen)
if (DOXYGEN_FOUND)
    # set input and output files
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # request to configure the file
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    message("Doxygen build started")

    add_custom_target( doc ALL
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM )
else ()
    message("Doxygen need to be installed to generate the doxygen documentation")
endif ()