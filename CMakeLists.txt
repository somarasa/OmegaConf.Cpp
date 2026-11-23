cmake_minimum_required(VERSION 3.24)
project(OmegaConfCpp VERSION 0.1.0 LANGUAGES CXX)

# Options
option(OMEGACONF_HEADER_ONLY "Build OmegaConf as a header-only library" ON)
option(BUILD_SHARED_LIBS "Build shared libraries (.dll / .so) in compiled mode" OFF)
option(OMEGACONF_BUILD_TESTS "Build test suite" ON)
option(OMEGACONF_BUILD_SAMPLES "Build sample applications" ON)
option(OMEGACONF_USE_CPP20 "Target C++20 standard (OFF targets C++17)" ON)

# Standard selection (C++20 default, C++17 compatible)
if(OMEGACONF_USE_CPP20)
    set(CMAKE_CXX_STANDARD 20)
else()
    set(CMAKE_CXX_STANDARD 17)
    add_compile_definitions(OMEGACONF_FORCE_CPP17=1)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows DLL & Visual Studio Organization
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Third-party dependencies
include(cmake/Dependencies.cmake)

# Collect headers for Visual Studio Solution Explorer
file(GLOB_RECURSE OMEGACONF_HEADERS CONFIGURE_DEPENDS "include/omegaconf/*.hpp")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include" PREFIX "Header Files" FILES ${OMEGACONF_HEADERS})

# Core Library (Header-only by default, compiled when OMEGACONF_HEADER_ONLY=OFF)
if(OMEGACONF_HEADER_ONLY)
    add_library(omegaconf INTERFACE)
    target_include_directories(omegaconf
        INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(omegaconf INTERFACE OMEGACONF_HEADER_ONLY=1)
    target_link_libraries(omegaconf
        INTERFACE
            ryml::ryml
            exprtk
    )
    if(OMEGACONF_USE_CPP20)
        target_link_libraries(omegaconf INTERFACE glaze::glaze)
    endif()
else()
    add_library(omegaconf
        ${OMEGACONF_HEADERS}
        src/version.cpp
        src/config_node.cpp
    )
    target_include_directories(omegaconf
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(omegaconf PUBLIC OMEGACONF_COMPILED_LIB=1)
    if(MSVC)
        target_compile_options(omegaconf PRIVATE /W4 /WX /permissive- /utf-8)
    else()
        target_compile_options(omegaconf PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-trigraphs)
    endif()
    target_link_libraries(omegaconf
        PUBLIC
            ryml::ryml
            exprtk
    )
    if(OMEGACONF_USE_CPP20)
        target_link_libraries(omegaconf PUBLIC glaze::glaze)
    endif()
endif()

add_library(omegaconf::omegaconf ALIAS omegaconf)

# Test Suite
if(OMEGACONF_BUILD_TESTS)
    enable_testing()
    add_executable(omegaconf_tests
        tests/test_main.cpp
        tests/test_ast.cpp
        tests/test_operators.cpp
        tests/test_parser.cpp
        tests/test_interpolation.cpp
        tests/test_tree_composition.cpp
        tests/test_cli_and_binding.cpp
        tests/test_hydra_parity.cpp
        tests/test_multirun_diff.cpp
        tests/test_official_hydra_suite.cpp
        tests/test_official_hydra_advanced.cpp
        tests/test_regression_and_coverage.cpp
        tests/test_stress_roundtrip.cpp
        tests/test_advanced_coverage.cpp
    )
    target_link_libraries(omegaconf_tests
        PRIVATE
            omegaconf
            Catch2::Catch2WithMain
    )
    if(MSVC)
        target_compile_options(omegaconf_tests PRIVATE /W4 /WX /permissive- /utf-8)
    else()
        target_compile_options(omegaconf_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-trigraphs)
    endif()

    include(CTest)
    include(Catch)
    catch_discover_tests(omegaconf_tests)
endif()

# Samples
if(OMEGACONF_BUILD_SAMPLES)
    add_executable(quickstart
        samples/quickstart/main.cpp
    )
    target_link_libraries(quickstart
        PRIVATE
            omegaconf
    )
    if(MSVC)
        target_compile_options(quickstart PRIVATE /W4 /WX /permissive- /utf-8)
    else()
        target_compile_options(quickstart PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-trigraphs)
    endif()
endif()
