# Developing with Visual Studio — OmegaConf.Cpp

This guide provides step-by-step instructions for building, running, and debugging **OmegaConf.Cpp** using **Visual Studio 2022 / 2019** on Windows.

---

## 1. Prerequisites

- **Visual Studio 2022** (v17.4 or newer) or **Visual Studio 2019** (v16.11 or newer)
- **Workload**: "Desktop development with C++"
  - MSVC C++ x64/x86 build tools
  - Windows 10 or 11 SDK
  - C++ CMake tools for Windows
- **CMake**: version 3.24 or higher

---

## 2. Generating the Solution & Projects

CMake generates native Visual Studio solutions (`.slnx` / `.sln`) and project files (`.vcxproj`).

### A. Default Static Library (`.lib`) Build
By default, OmegaConf.Cpp builds as a **static library** (`omegaconf.lib`), embedding all symbols directly into your binary with zero runtime DLL dependencies:

```powershell
# For Visual Studio 2022 (x64)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Or let CMake automatically pick the newest installed Visual Studio:
cmake -B build
```

### B. Shared Library / DLL (`.dll`) Build
To compile OmegaConf.Cpp as a **dynamic link library** (`omegaconf.dll` and import library `omegaconf.lib`), set `-DBUILD_SHARED_LIBS=ON`:

```powershell
cmake -B build -DBUILD_SHARED_LIBS=ON
```

> [!NOTE]
> When building as a DLL on Windows, CMake automatically sets unified runtime directories (`build/bin/Release` / `build/bin/Debug`) and enables `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS`, which generates the module definition export file (`.def`) so all public functions and AST nodes are properly exported without requiring manual `__declspec(dllexport)` annotations.

### C. Standard Selection: C++17 vs C++20+
OmegaConf.Cpp natively supports both strict **C++17** (`/std:c++17`) and modern **C++20** (`/std:c++20`):

- **Targeting C++20 (Default)**:
  Uses Glaze for zero-macro compile-time reflection.
  ```powershell
  cmake -B build -DOMEGACONF_USE_CPP20=ON
  ```

- **Targeting C++17**:
  Uses SFINAE, polyfill string utilities, and the lightweight reflection macro `OMEGACONF_REFLECT`.
  ```powershell
  cmake -B build -DOMEGACONF_USE_CPP20=OFF
  ```
  Alternatively, you can pass `-DOMEGACONF_FORCE_CPP17=1` or define `#define OMEGACONF_FORCE_CPP17 1` in preprocessor settings. Both static library and DLL modes are 100% supported in C++17 mode.

---

## 3. Opening the Solution in Visual Studio

You have two convenient ways to open and develop in Visual Studio:

### Option A: Open Generated Solution (Recommended)
You can launch Visual Studio directly from the terminal:
```powershell
cmake --open build
```
Or open Visual Studio, click **File → Open → Project/Solution**, and navigate to:
```text
D:\WorkSpace\Antigravity\OmegaConf.Cpp\build\OmegaConfCpp.slnx
```
*(On older versions of CMake/VS, this will be `build\OmegaConfCpp.sln`).*

### Option B: Visual Studio Native "Open Folder" Mode
1. Launch Visual Studio.
2. Select **File → Open → Folder...** and pick `D:\WorkSpace\Antigravity\OmegaConf.Cpp`.
3. Visual Studio automatically detects `CMakeLists.txt` and configures the project. You can switch between `x64-Debug` and `x64-Release` in the configuration dropdown.

---

## 4. Visual Studio Solution Explorer Layout

The solution is organized into clear project and folder hierarchies:

```text
Solution 'OmegaConfCpp'
├── omegaconf                      # Core library target (Static lib or DLL)
│   ├── Header Files               # Mirrored folder hierarchy matching include/
│   │   └── omegaconf/
│   │       ├── ast/               # ConfigNode, ConfigValueNode, ConfigObjectNode...
│   │       ├── binding/           # Struct reflection & deserialization
│   │       ├── builder/           # OmegaConfBuilder fluent API
│   │       ├── callbacks/         # Lifecycle event callbacks
│   │       ├── cli/               # Overrides parser, alias expansion
│   │       ├── common/            # Exceptions, SourceInfo
│   │       ├── composition/       # DefaultsList, TreeResolver, ConfigStore
│   │       ├── diff/              # ConfigDiffer tree comparison
│   │       ├── interpolation/     # Variable interpolation, resolvers
│   │       ├── multirun/          # SweepEngine, cartesian product
│   │       ├── operators/         # Mutation operators (+=, -=, *=, /=, etc.)
│   │       ├── parsers/           # YAML/JSON/JSONC/TOML format parser
│   │       ├── serialization/     # YAML/JSON serializers
│   │       └── omegaconf.hpp      # Master include header
│   └── Source Files
│       ├── config_node.cpp
│       └── version.cpp
├── omegaconf_tests                # Catch2 unit & integration test executable (99 test cases)
└── quickstart                     # Sample runnable executable
```

---

## 5. Building & Debugging Inside Visual Studio

### Building All or Specific Targets
- Press **Ctrl + Shift + B** (or **Build → Build Solution**) to build all projects.
- To build just the tests, right-click `omegaconf_tests` in Solution Explorer and choose **Build**.

### Running and Debugging Tests
1. Right-click `omegaconf_tests` in Solution Explorer.
2. Select **Set as Startup Project**.
3. Press **F5** to start debugging with full breakpoint support, or **Ctrl + F5** to run without debugger.

### Running Specific Test Cases
You can pass Catch2 test filters via project properties:
1. Right-click `omegaconf_tests` → **Properties**.
2. Go to **Configuration Properties → Debugging**.
3. Set **Command Arguments** to any tag or pattern, for example:
   - `"[interpolation]"` (runs all interpolation tests)
   - `"[cli]"` (runs all CLI override tests)
   - `"*Cascading*"` (runs cascading directory tests)
   - `"-s"` (shows successful assertion details)

### Visual Studio Test Explorer
Visual Studio natively discovers Catch2 test cases:
1. Open **Test → Test Explorer** (**Ctrl + E, T**).
2. Click **Run All Tests in View**.
3. You will see all 99 test cases with green checkmarks, detailed execution timings, and instant jump-to-source on click.

---

## 6. Command Line Build & Test Reference

You can also build and test directly from the command line using MSBuild or CMake:

```powershell
# 1. Build Release configuration
cmake --build build --config Release

# 2. Build Debug configuration
cmake --build build --config Debug

# 3. Run all tests via CTest
ctest --test-dir build --output-on-failure -C Release

# 4. Run Catch2 test executable directly
.\build\Release\omegaconf_tests.exe
```

---

## 7. Consuming OmegaConf.Cpp in Another Project

### Linking as a Static Library
```cmake
add_subdirectory(path/to/OmegaConf.Cpp)
target_link_libraries(my_application PRIVATE omegaconf)
```

### Linking as a Dynamic Library (DLL)
```cmake
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
add_subdirectory(path/to/OmegaConf.Cpp)
target_link_libraries(my_application PRIVATE omegaconf)

# On Windows, copy omegaconf.dll to output folder for debugging:
add_custom_command(TARGET my_application POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:omegaconf> $<TARGET_FILE_DIR:my_application>
)
```
