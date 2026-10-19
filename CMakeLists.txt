cmake_minimum_required(VERSION 3.14)
project(meansort LANGUAGES CXX VERSION 1.0.0)

add_library(meansort INTERFACE)
target_include_directories(meansort INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(meansort INTERFACE cxx_std_17)
add_library(meansort::meansort ALIAS meansort)

find_package(Threads REQUIRED)
target_link_libraries(meansort INTERFACE Threads::Threads)

option(MEANSORT_BUILD_TESTS "构建测试" OFF)
option(MEANSORT_BUILD_BENCH "构建基准测试" OFF)

if(MEANSORT_BUILD_TESTS)
    enable_testing()
    add_executable(meansort_test tests/test_meansort.cpp)
    target_link_libraries(meansort_test PRIVATE meansort)
    add_test(NAME meansort_test COMMAND meansort_test)
endif()

if(MEANSORT_BUILD_BENCH)
    add_executable(meansort_bench benchmark/bench.cpp)
    target_link_libraries(meansort_bench PRIVATE meansort)
    target_compile_options(meansort_bench PRIVATE -O3)
endif()

install(DIRECTORY include/ DESTINATION include)
install(TARGETS meansort EXPORT meansortConfig)
install(EXPORT meansortConfig NAMESPACE meansort:: DESTINATION lib/cmake/meansort)
