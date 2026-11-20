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

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```
