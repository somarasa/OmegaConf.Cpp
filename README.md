# OmegaConf.Cpp

[![CI](https://github.com/somarasa/OmegaConf.Cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/somarasa/OmegaConf.Cpp/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17%20%7C%2020%2B-00599C?logo=c%2B%2B)](CMakeLists.txt)
[![Header Only](https://img.shields.io/badge/library-Header--Only%20(Default)-green.svg)](CMakeLists.txt)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)](CMakeLists.txt)

A hierarchical, composable configuration framework for modern C++ (C++20 & C++17) inspired by Python's **Hydra** and **OmegaConf**.

## Features
- 📂 **Directory-based Config Groups & Defaults Lists** with `_self_` precedence.
- 🔀 **Multi-level Automatic Cascading** and explicit inheritance (`_inherits_`).
- ⚡ **In-Config Mutation Operators** (`+=`, `-=`, `*=`, `/=`, `&=`, `|=`, `!=`).
- 🔗 **Variable Interpolation & Resolvers** with relative dot navigation, `${env:...}`, and `${eval:...}`.
- 🧩 **Zero-Boilerplate Struct Binding** via compile-time reflection.
- 💻 **Hydra-style CLI Overrides & Sweeps** (`+`, `++`, `~`, `choice()`, `range()`, `-m`).

## Requirements
- C++20 or C++17 compliant compiler (MSVC 2022+, GCC 11+, or Clang 15+)
- CMake 3.24+

## Integration Options

### 1. Header-Only (Default)
OmegaConf.Cpp is **Header-Only by default** (`OMEGACONF_HEADER_ONLY=ON`). Simply `#include <omegaconf/omegaconf.hpp>` in your project with zero library compilation:

```cmake
# CMake integration via FetchContent
include(FetchContent)
FetchContent_Declare(
    omegaconf
    GIT_REPOSITORY https://github.com/somarasa/OmegaConf.Cpp.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(omegaconf)

target_link_libraries(your_target PRIVATE omegaconf::omegaconf)
```

### 2. Compiled Library (Static or DLL)
For larger codebases wishing to precompile the core AST and serialization translation units:
- **Static Library (`.lib` / `.a`)**:
  ```bash
  cmake -B build -DOMEGACONF_HEADER_ONLY=OFF -DBUILD_SHARED_LIBS=OFF
  cmake --build build --config Release
  ```
- **Shared Library / DLL (`.dll` / `.so`)**:
  ```bash
  cmake -B build -DOMEGACONF_HEADER_ONLY=OFF -DBUILD_SHARED_LIBS=ON
  cmake --build build --config Release
  ```

## C++ Standard Selection (C++20 vs C++17)
OmegaConf.Cpp supports both **modern C++20+** and **strict C++17**:
- **C++20 Mode (Default)**:
  Zero-macro automatic struct reflection via Glaze.
  ```bash
  cmake -B build -DOMEGACONF_USE_CPP20=ON
  ```
- **C++17 Mode**:
  Polyfill string utilities and lightweight macro-assisted struct reflection (`OMEGACONF_REFLECT`).
  ```bash
  cmake -B build -DOMEGACONF_USE_CPP20=OFF
  ```

## Building & Testing
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

## Visual Studio
For comprehensive Visual Studio instructions (opening `.slnx` solutions, Solution Explorer layout, and Test Explorer debugging), see [VISUAL_STUDIO.md](VISUAL_STUDIO.md).
