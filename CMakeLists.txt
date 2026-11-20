cmake_minimum_required(VERSION 3.24)
project(OmegaConfCpp VERSION 0.1.0 LANGUAGES CXX)

# Require C++20 across all platforms
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd / IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(OMEGACONF_BUILD_TESTS "Build test suite" ON)
option(OMEGACONF_BUILD_SAMPLES "Build sample applications" ON)

# Third-party dependencies
include(cmake/Dependencies.cmake)

# Core Library
add_library(omegaconf
    src/version.cpp
)

target_include_directories(omegaconf
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compiler Warnings & Strictness
if(MSVC)
    target_compile_options(omegaconf PRIVATE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(omegaconf PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Link dependencies to core library
target_link_libraries(omegaconf
    PUBLIC
        ryml::ryml
        glaze::glaze
        exprtk
)

# Test Suite
if(OMEGACONF_BUILD_TESTS)
    enable_testing()
    add_executable(omegaconf_tests
        tests/test_main.cpp
    )
    target_link_libraries(omegaconf_tests
        PRIVATE
            omegaconf
            Catch2::Catch2WithMain
    )
    if(MSVC)
        target_compile_options(omegaconf_tests PRIVATE /W4 /WX /permissive- /utf-8)
    else()
        target_compile_options(omegaconf_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
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
        target_compile_options(quickstart PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()
