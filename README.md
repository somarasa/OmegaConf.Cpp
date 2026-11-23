# OmegaConf.Cpp

A hierarchical, composable configuration framework for modern C++ (C++20) inspired by Python's **Hydra** and **OmegaConf**.

## Features (Roadmap)
- 📂 **Directory-based Config Groups & Defaults Lists** with `_self_` precedence.
- 🔀 **Multi-level Automatic Cascading** and explicit inheritance (`_inherits_`).
- ⚡ **In-Config Mutation Operators** (`+=`, `-=`, `*=`, `/=`, `&=`, `|=`, `!=`).
- 🔗 **Variable Interpolation & Resolvers** with relative dot navigation, `${env:...}`, and `${eval:...}`.
- 🧩 **Zero-Boilerplate Struct Binding** via compile-time reflection.
- 💻 **Hydra-style CLI Overrides & Sweeps** (`+`, `++`, `~`, `choice()`, `range()`, `-m`).

## Requirements
- C++20 compliant compiler (MSVC 2022+, GCC 11+, or Clang 15+)
- CMake 3.24+

## Building & Testing

### Quick Start (Static Library)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

### Static Library (`.lib`) vs Dynamic Library (`.dll`)
OmegaConf.Cpp supports both static and dynamic linking:
- **Static Library (`omegaconf.lib`)**:
  ```bash
  cmake -B build -DBUILD_SHARED_LIBS=OFF
  cmake --build build --config Release
  ```
- **Shared Library / DLL (`omegaconf.dll`)**:
  ```bash
  cmake -B build -DBUILD_SHARED_LIBS=ON
  cmake --build build --config Release
  ```

### C++ Standard Selection (C++17 vs C++20+)
OmegaConf.Cpp supports both **strict C++17** and modern **C++20+**:
- **C++20 Mode (Default)**:
  Zero-macro automatic struct binding via Glaze reflection.
  ```bash
  cmake -B build -DOMEGACONF_USE_CPP20=ON
  ```
- **C++17 Mode**:
  Polyfill string utilities and lightweight macro-assisted struct reflection (`OMEGACONF_REFLECT`).
  ```bash
  cmake -B build -DOMEGACONF_USE_CPP20=OFF
  ```
  Or define `-DOMEGACONF_FORCE_CPP17=1` / `#define OMEGACONF_FORCE_CPP17 1` in code.

## Visual Studio
For comprehensive Visual Studio instructions (opening `.slnx` solutions, Solution Explorer layout, and Test Explorer debugging), see [VISUAL_STUDIO.md](VISUAL_STUDIO.md).
