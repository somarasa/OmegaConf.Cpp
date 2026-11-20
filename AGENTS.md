# AGENTS.md — AI Agent Guidelines & Architecture Manual for OmegaConf.Cpp

> **Scope**: This document defines the operating rules, architectural standards, and workflow directives for autonomous AI coding agents contributing to the **OmegaConf.Cpp** codebase.

---

## 1. Project Overview & References

**OmegaConf.Cpp** is a hierarchical, composable configuration framework for modern C++ (C++20) inspired by Python's Hydra, OmegaConf, and our reference C# implementation [OmegaConf.NET](file:///D:/WorkSpace/Antigravity/OmegaConf.NET).

### Primary References Available on This Machine:
- **C# Port (Algorithm & Test Source)**: `D:\WorkSpace\Antigravity\OmegaConf.NET`
  - AST, TreeResolver, Operators, Interpolation, and 137 test specs in `tests/OmegaConf.NET.Tests/`.
- **Python Upstream**: `D:\WorkSpace\Antigravity\hydra`
  - Official Python docs and test suites.

---

## 2. Agent Operating Directives & Build Rules

1. **C++20 Standard Mandate**:
   - The entire codebase targets strict **C++20** (`set(CMAKE_CXX_STANDARD 20)`).
   - Use standard library capabilities where available: `std::filesystem`, `std::string_view`, `std::optional`, `std::variant`, `std::span`, concepts.

2. **Zero Warning Standard**:
   - All code must build with **0 errors and 0 warnings**.
   - MSVC: `/W4 /WX /permissive- /utf-8`.
   - GCC / Clang: `-Wall -Wextra -Wpedantic -Werror`.

3. **Single-Node / Deterministic Builds**:
   - Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
   - Build: `cmake --build build --config Release`
   - Test: `ctest --test-dir build --output-on-failure -C Release`

4. **Third-Party Library Standards (via FetchContent)**:
   - **ryml (rapidyaml)**: YAML parsing & emission.
   - **glaze**: Zero-overhead compile-time struct reflection & binding.
   - **exprtk**: In-config math, logic, and string `${eval:...}` evaluator.
   - **Catch2 v3**: Unit and integration test runner.

5. **Test-First Requirement**:
   - Every ported component must have accompanying Catch2 tests in `tests/` matching the behavioral tests in `OmegaConf.NET.Tests`.
   - Never mark a feature complete without running and passing the test suite.

---

## 3. Core Architecture Plan

```text
OmegaConf.Cpp/
├── CMakeLists.txt
├── cmake/
│   └── Dependencies.cmake
├── include/
│   └── omegaconf/
│       ├── ast/               # ConfigNode, ConfigValueNode, ConfigListNode, ConfigObjectNode
│       ├── composition/       # DefaultsList, TreeResolver, SearchPaths, PackageDirectives
│       ├── operators/         # Mutation operators (+=, -=, *=, /=, &=, |=, !=)
│       ├── interpolation/     # Variable interpolation, relative dots, resolvers (env, eval, now)
│       ├── binding/           # Struct reflection & deserialization via glaze / boost::pfr
│       ├── cli/               # CLI parser, prefix overrides (+, ++, ~), multirun sweep generator
│       └── omegaconf.hpp      # Master include
├── src/                       # Implementation files
├── tests/                     # Catch2 test suites
└── samples/                   # Runnable examples
```
