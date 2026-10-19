cmake_minimum_required(VERSION 3.14)
project(meansort LANGUAGES CXX VERSION 1.0.0)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(meansort INTERFACE)
add_library(meansort::meansort ALIAS meansort)
target_include_directories(meansort INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(meansort INTERFACE cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(meansort INTERFACE Threads::Threads)

option(MEANSORT_BUILD_TESTS "构建测试" OFF)
option(MEANSORT_BUILD_BENCH "构建基准测试" OFF)
option(MEANSORT_BUILD_BENCH_LEGACY
       "构建旧算法基准(强制三路划分,仅用于 A/B 对比)" OFF)

if(MEANSORT_BUILD_TESTS)
    enable_testing()
    add_executable(meansort_test tests/test_meansort.cpp)
    target_link_libraries(meansort_test PRIVATE meansort::meansort)
    if(MSVC)
        # 源码含中文字面量:MSVC 默认按本地代码页解析,需要显式指定 UTF-8
        target_compile_options(meansort_test PRIVATE /utf-8)
    endif()
    add_test(NAME meansort_test COMMAND meansort_test)
endif()

if(MEANSORT_BUILD_BENCH)
    add_executable(meansort_bench benchmark/bench.cpp)
    target_link_libraries(meansort_bench PRIVATE meansort::meansort)
    if(MSVC)
        target_compile_options(meansort_bench PRIVATE /O2 /utf-8)
    else()
        target_compile_options(meansort_bench PRIVATE -O3)
    endif()
endif()

if(MEANSORT_BUILD_BENCH_LEGACY)
    add_executable(meansort_bench_legacy benchmark/bench.cpp)
    target_link_libraries(meansort_bench_legacy PRIVATE meansort::meansort)
    target_compile_definitions(meansort_bench_legacy PRIVATE MEANSORT_ALWAYS_THREE_WAY)
    if(MSVC)
        target_compile_options(meansort_bench_legacy PRIVATE /O2 /utf-8)
    else()
        target_compile_options(meansort_bench_legacy PRIVATE -O3)
    endif()
endif()

# ---------------------------------------------------------------------------
# 安装:头文件 + 可用的 find_package(meansort CONFIG)
# 之前的版本只有 install(EXPORT ...),没有生成 meansortConfig.cmake,
# 所以装完之后 find_package(meansort CONFIG) 是找不到包的。
# ---------------------------------------------------------------------------
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS meansort EXPORT meansortTargets)

install(EXPORT meansortTargets
    FILE meansortTargets.cmake
    NAMESPACE meansort::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/meansort)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/meansortConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/meansortConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/meansort)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/meansortConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/meansortConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/meansortConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/meansort)
