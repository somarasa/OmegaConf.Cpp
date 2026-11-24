# OmegaConf.Cpp

[![CI](https://github.com/somarasa/OmegaConf.Cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/somarasa/OmegaConf.Cpp/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17%20%7C%2020%2B-00599C)](CMakeLists.txt)
[![Library](https://img.shields.io/badge/Library-Header--Only%20(Default)-green)](CMakeLists.txt)
[![AI](https://img.shields.io/badge/AI-Assisted-blueviolet)](https://github.com/somarasa/OmegaConf.Cpp)

OmegaConf.Cpp is a hierarchical, composable configuration framework for modern C++ (C++20 & C++17) inspired by Python's [Hydra](https://hydra.cc/) and [OmegaConf](https://omegaconf.readthedocs.io/).

It provides tree-based configuration composition, automatic inheritance cascading, in-config mutation operators (`+=`, `-=`, `*=`, `/=`, `&=`, `|=`, `!=`), compile-time struct reflection, variable interpolation, parameter sweeps, and Hydra-compliant CLI overrides.

---

## Features

- **Hierarchical Composition**: Modular config groups (`db/`, `model/`, `server/`) composed via `defaults` lists with `_self_` precedence control.
- **Auto-Cascading & Explicit Inheritance**: Nested configuration files cascade automatically on base files sharing the same name (`db/staging/postgres.yaml` inherits from `db/postgres.yaml`). Also supports explicit `_inherits_: "../base.yaml"`.
- **In-Config Mutation Operators**: Key suffixes mutate inherited or existing values (`+=`, `-=`, `*=`, `/=`, `&=`, `|=`, `!=`).
- **Format Agnostic**: Unified AST parsing and serialization across YAML, JSON / JSONC, and TOML.
- **Variable Interpolation**:
  - `${path.to.key}` with native type preservation (integers remain integers, booleans remain booleans).
  - Relative dot navigation (`${.sibling}`, `${..parent}`).
  - Built-in math and logic evaluation via `${eval:...}`.
  - Environment variables: `${env:VAR, fallback}` and `${oc.env:VAR, fallback}`.
  - Custom resolver registration via `OmegaConf::register_resolver()`.
  - Circular reference and recursion limit detection.
- **Mandatory Value Markers (`???`)**: Mark keys that must be supplied by child layers or command-line arguments.
- **Command-Line Overrides**: Override group selections (`db=production/postgres`), drop groups (`~db`), mutate values (`threads+=2`, `debug!=true`), or enforce strict mode (`+key=val`, `++key=val`).
- **Parameter Sweeps (Multi-Run)**: Cartesian product sweeps across comma-separated values or generator functions (`range()`, `choice()`, `glob()`).
- **Zero-Boilerplate Struct Reflection**:
  - **C++20**: Pure compile-time automatic struct binding via Glaze.
  - **C++17**: Lightweight macro-assisted struct binding (`OMEGACONF_REFLECT`).
  - Automatic casing translation (`PascalCase` to `snake_case`, `kebab-case`, or exact match).

---

## Integration

### 1. Header-Only (Default)
OmegaConf.Cpp is header-only by default (`OMEGACONF_HEADER_ONLY=ON`). Include `<omegaconf/omegaconf.hpp>` with zero library compilation:

```cmake
include(FetchContent)
FetchContent_Declare(
    omegaconf
    GIT_REPOSITORY https://github.com/somarasa/OmegaConf.Cpp.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(omegaconf)

target_link_libraries(your_target PRIVATE omegaconf::omegaconf)
```

### 2. Compiled Library (Static or DLL)
For large codebases requiring precompiled translation units:
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

---

## C++ Standard Selection

- **C++20 Mode (Default)**:
  Automatic struct binding without macros.
  ```bash
  cmake -B build -DOMEGACONF_USE_CPP20=ON
  ```
- **C++17 Mode**:
  Polyfill string utilities and macro-assisted reflection.
  ```bash
  cmake -B build -DOMEGACONF_USE_CPP20=OFF
  ```

---

## Quick Start

### 1. Configuration Files Layout

```text
configs/
├── config.yaml               # Root entrypoint
└── db/
    ├── postgres.yaml         # Base defaults
    ├── sqlite.yaml
    ├── staging/
    │   └── postgres.yaml     # Staging overrides (cascades on db/postgres.yaml)
    └── production/
        └── postgres.yaml     # Production overrides with operators
```

#### `configs/config.yaml`
```yaml
defaults:
  - db: staging/postgres
  - _self_

app_name: "CatalogService"
active_workers: 4
```

#### `configs/db/postgres.yaml` (Base)
```yaml
driver: postgres
port: 5432
max_connections: 50
ssl: false
timeout_seconds: 30
```

#### `configs/db/staging/postgres.yaml` (Overrides)
```yaml
host: "staging-db.internal"
max_connections: 20
```

#### `configs/db/production/postgres.yaml` (Overrides with Operators)
```yaml
host: "prod-db.internal"
max_connections*=: 4          # 50 * 4 = 200
ssl: true
timeout_seconds+=: 30         # 30 + 30 = 60
```

---

### 2. Define C++ Structs

#### C++20 Mode (Zero Boilerplate)
```cpp
#include <string>
#include <optional>

struct DatabaseConfig {
    std::string driver = "postgres";
    std::string host = "localhost";
    int port = 5432;
    int max_connections = 0;
    bool ssl = false;
    int timeout_seconds = 0;
};

struct AppConfig {
    std::string app_name;
    std::optional<DatabaseConfig> db;
    int active_workers = 4;
};
```

#### C++17 Mode (Macro-Assisted)
```cpp
#include <string>
#include <optional>
#include <omegaconf/omegaconf.hpp>

struct DatabaseConfig {
    std::string driver = "postgres";
    std::string host = "localhost";
    int port = 5432;
    int max_connections = 0;
    bool ssl = false;
    int timeout_seconds = 0;
};
OMEGACONF_REFLECT(DatabaseConfig, driver, host, port, max_connections, ssl, timeout_seconds)

struct AppConfig {
    std::string app_name;
    std::optional<DatabaseConfig> db;
    int active_workers = 4;
};
OMEGACONF_REFLECT(AppConfig, app_name, db, active_workers)
```

---

### 3. Load & Bind

```cpp
#include <iostream>
#include <omegaconf/omegaconf.hpp>

int main(int argc, char* argv[]) {
    auto config = omegaconf::OmegaConfBuilder::create()
        .set_root("configs")
        .add_cli_args(argc, argv) // e.g. db=production/postgres active_workers+=2
        .build<AppConfig>();

    std::cout << "App: " << config.app_name << "\n";
    if (config.db) {
        std::cout << "Database: " << config.db->host << ":" << config.db->port
                  << " (Connections: " << config.db->max_connections << ")\n";
    }
    return 0;
}
```

---

## Operators Reference

| Operator | Action | Target Types | Example |
| :---: | :--- | :--- | :--- |
| `=` | Assignment / replace | Any | `port=: 5432` |
| `+=` | Add / concatenate / append | Numbers, Strings, Lists, Objects | `timeout+=: 10`, `tags+=: ["gpu"]` |
| `-=` | Subtraction / item removal | Numbers, Lists | `retry_count-=: 1`, `items-=: ["old"]` |
| `*=` | Multiplication / repeat | Numbers, Strings | `workers*=: 2`, `"ha"*=: 3` |
| `/=` | Division | Numbers | `rate/=: 2` |
| `&=` | Logical / bitwise AND | Booleans, Integers | `strict_mode&=: false` |
| `|=` | Logical / bitwise OR | Booleans, Integers | `enable_feature|=: true` |
| `!=` | Boolean toggle / invert | Booleans | `debug!=: true` |

---

## Command-Line Overrides & Sweeps

### CLI Overrides
```bash
# Switch config group choice
./app db=production/postgres

# Drop a config group
./app ~db

# Override properties with mutation operators
./app db.max_connections=500 db.timeout_seconds+=15 active_workers+=4 debug!=true

# Struct mode (safe add / upsert)
./app +db.pool_size=20 ++db.port=5433
```

### Parameter Sweeps (`-m` / `--multirun`)
```bash
./app -m db=staging/postgres,production/postgres active_workers=2,4,8
```

Programmatic sweep execution:
```cpp
std::vector<AppConfig> configs = omegaconf::OmegaConfBuilder::create()
    .set_root("configs")
    .add_cli_args(argc, argv)
    .build_sweeps<AppConfig>();
```

---

## Building & Testing

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

For Visual Studio solutions (`.slnx`), Solution Explorer layout, and Test Explorer integration, see [VISUAL_STUDIO.md](VISUAL_STUDIO.md).

---

## License

This project is licensed under the [MIT License](LICENSE).
