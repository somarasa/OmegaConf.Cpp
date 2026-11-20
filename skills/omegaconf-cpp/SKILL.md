---
name: omegaconf-cpp
description: Authoritative guide for AI coding assistants to design, generate, refactor, and integrate hierarchical configurations in C++ using OmegaConf.Cpp.
---

# OmegaConf.Cpp Skill Guide for AI Assistants

Use this skill when you need to create, modify, or integrate configuration trees in C++ using **OmegaConf.Cpp**.

OmegaConf.Cpp brings the best features of Python's **Hydra** and **OmegaConf** to the modern C++ (C++20) ecosystem:
- Directory-based **Config Groups** and **Defaults Lists**.
- **Multi-Config Selection**: selecting several configs from one group **merges** them into a keyed object at the group package — namespace each with `@package group.<name>` to keep them distinct.
- **Multi-level Automatic Cascading** and cross-format explicit inheritance (`_inherits_`).
- In-config **mutation operators** (`+=`, `-=`, `*=`, `/=`, `&=`, `|=`, `!=`).
- **Variable interpolation** with list indexing (`${servers[0]}`), relative dot navigation, literal escaping, `${eval:...}`, and `${env:...}`.
- **Zero-Boilerplate Structs Binding** via compile-time reflection (`glaze` / `boost::pfr`).
- **Format Agnostic Parsing & Serialization** across YAML, JSON, and TOML.
- **Hydra-style CLI Overrides & Parameter Sweeps** (`+`, `++`, `~`, `choice()`, `range()`, `-m`).

---

## 1. Directory Tree & Config Composition

### Recommended Layout
Place configs in a folder (e.g. `configs/` or `conf/`):

```text
configs/
├── config.yaml               # Root entrypoint
├── db/                       # Config group: 'db'
│   ├── postgres.yaml         # Base postgres defaults
│   ├── sqlite.yaml
│   ├── staging/
│   │   └── postgres.yaml     # Staging overrides (auto-cascades on db/postgres.yaml)
│   └── production/
│       └── postgres.yaml     # Production overrides (uses operators)
└── model/                    # Config group: 'model'
    ├── fast.yaml
    └── accurate.yaml
```

### The Defaults List (`config.yaml`)
Always declare the `defaults:` list in your entrypoint file:

```yaml
defaults:
  - db: staging/postgres        # Resolves db/postgres.yaml -> db/staging/postgres.yaml
  - db@replica_db: sqlite       # Mounts db/sqlite.yaml under 'replica_db' (@package directive)
  - experiment@_global_: fast   # Merges experiment/fast.yaml directly into root (@_global_)
  - model: ${default_model}     # Interpolated default entry (supports properties, env vars, or earlier choices)
  - optional secrets: local     # Gracefully ignored if local.yaml doesn't exist
  - _self_                      # Determines precedence of local settings below

app_name: "CatalogService"
active_workers: 4
```

### The `_self_` Keyword
- If `_self_` is placed at the **end** (or omitted), settings in `config.yaml` **override** the defaults.
- If `_self_` is placed at the **beginning**, the defaults **override** `config.yaml`.
- `_self_` can appear at most **once** in any defaults list (duplicates throw `ConfigSyntaxException`).

### Defaults List Directives & Syntax Rules
- **No Parent Directory Traversal (`..`)**: Defaults lists strictly forbid `..` (`- ../base` or `- db: ../sqlite` is rejected). Paths must be relative to the group directory or absolute from the configuration root starting with `/` (e.g. `- /shared/base` or `- /cloud/aws@_group_.primary: rds`). To inherit from parent files outside the tree, use `_inherits_: "../base.yaml"` or an absolute rooted path `- /path`.
- **Overriding in Child Configs (`override`)**: If a child configuration changes a default choice selected by a parent base, prefix it with `override`:
  ```yaml
  defaults:
    - override db: mysql
  ```
  Specifying conflicting choices for the same group without `override` throws `ConfigSyntaxException`.
- **Disabling a Group (`null`)**: Set `- group: null` (or CLI `group=null`) to skip loading any configuration from that group.
- **Mandatory Defaults (`???`)**: Mark a group as `- group: ???` when a choice must be supplied by the caller via CLI (`group=choice`). If left unresolved at runtime, throws `MissingValueMarkerException`.
- **Optional Defaults (`optional`)**: Prefix with `optional` (`- optional secrets: local`) to gracefully ignore missing configuration files without error.
- **Slash Normalization in Subgroups**: If a choice selects a subgroup like `- group_a: sub_group/item`, any relative defaults declared inside `item.yaml` resolve inside `group_a/sub_group/`.

### Package Directive Precedence
When determining where a configuration file mounts in the output object tree, OmegaConf.Cpp follows Hydra's strict precedence order:
1. **Defaults List Package Directive**: `- group@custom_pkg: choice` (or CLI `+group@pkg=choice`) has highest precedence.
2. **File Header Directive**: `# @package custom_pkg` in the target file.
   > **Note**: Never put inline comments on the `# @package` line (e.g. `# @package foo # comment` will corrupt the package name). Keep comments on separate lines.
3. **Config Group Name**: Default fallback derived from the group folder name (e.g. `db/` mounts under `db`).

Special package keywords:
- `@_global_`: Mounts keys directly at the root object level.
- `@_group_`: Refers to the config group name (e.g. `- /cloud/aws@_group_.primary: rds` mounts at `cloud.aws.primary`).

### Automatic Cascading vs. Explicit Inheritance
- **Auto-Cascading (Default)**: If a child file like `db/staging/postgres.yaml` shares the same filename as its parent `db/postgres.yaml`, the base file is loaded automatically first. Only write diffs in the child file!
- **Explicit Inheritance**: To inherit from an arbitrary file, declare `_inherits_`:
  ```yaml
  _inherits_: "../base_db.yaml"
  # Or multiple bases:
  _inherits_: ["base1.yaml", "base2.yaml"]
  ```

### Selecting Multiple Configs from a Group (Multi-Config)
You can select **several** configs from one group at once by passing a list in the Defaults List (or on the command line):

```yaml
defaults:
  - services: [postgres, temporal]   # two files from the services/ group
```

**Composition MERGES — it does not build a list.** OmegaConf.Cpp mounts every selected file under the group's package and **merges** them into a single object. It never concatenates them into an array:

To keep each file distinct, namespace them with a `# @package group.<name>` header:
```yaml
# services/postgres.yaml
# @package services.postgres
name: postgres
port: 5432
# services/temporal.yaml
# @package services.temporal
name: temporal
port: 7233
# =>
services:
  postgres: { name: postgres, port: 5432 }
  temporal: { name: temporal, port: 7233 }
```

Bind to a `std::unordered_map<std::string, ServiceConfig>` in C++:
```cpp
struct AppConfig {
    std::string app_name;
    std::unordered_map<std::string, ServiceConfig> services;
};
```

---

## 2. In-Config Mutation Operators

OmegaConf.Cpp uses **key suffixes** to define mutation operators in YAML, JSON, and TOML.

| Operator | Key Syntax | Target Types | Behavior |
| :---: | :--- | :--- | :--- |
| `=` | `key=:` | Any | Direct assignment / replace |
| `+=` | `key+=:` | Numbers, Strings, Lists, Objects | Numeric add, string concat, append list items, merge object |
| `-=` | `key-=:` | Numbers, Lists | Numeric subtraction, remove matching list items |
| `*=` | `key*=:` | Numbers, Strings | Numeric multiplication, string repetition (`"ha"*=: 3` -> `"hahaha"`) |
| `/=` | `key/=:` | Numbers | Numeric division |
| `&=` | `key&=:` | Booleans, Integers | Logical AND, Bitwise AND |
| `|=` | `key|=:` | Booleans, Integers | Logical OR, Bitwise OR |
| `!=` | `key!=:` | Booleans | Logical toggle / invert current boolean value |

```yaml
# In child configuration:
max_connections*=: 4          # Multiply inherited 50 by 4 = 200
timeout_seconds+=: 30         # Add 30 to inherited timeout
tags+=: ["gpu", "cluster"]    # Append to inherited list
features-=: ["legacy_v1"]     # Remove item from inherited list
enable_ssl!=: true            # Invert inherited boolean
```

---

## 3. Variable Interpolation & Resolvers

### Interpolation Syntax
- **Absolute Reference**: `${server.port}` or `${db.postgres.host}`.
- **List Indexing**: `${servers[0]}` or `${servers.0}`.
- **Native Type Preservation**: When a string is solely `${server.port}`, OmegaConf.Cpp preserves the native `int` type (rather than converting to `"8080"`).
- **Default Fallback**: `${cache.ttl:300}` (uses `300` if `cache.ttl` is not found).

### Relative & Sibling Navigation (Dot Syntax)
- **`.sibling`**: Reference a key in the **same enclosing object**:
  ```yaml
  db:
    port: 5432
    url: "postgresql://localhost:${.port}/app"
  ```
- **`..parent_sibling`**: Reference a key in the **parent object** (one level up):
  ```yaml
  db:
    host: "db.internal"
    staging:
      port: 5433
      connection: "Server=${..host};Port=${.port}"
  ```
- **Inside List Elements (`.` / `..` / `...`)**:
  - **`${.0}`**: References sibling index `0` of the *current list*.
  - **`${..sibling_key}`**: Escapes the list to reference a field in the enclosing container.
  - **`${...root_key}`**: Escapes the container up to the grandparent root.

### Literal Escaping
To output a literal `${...}`, prefix with a backslash:
```yaml
script: "echo \\${HOME} on port ${server.port}"
# Resolves to: "echo ${HOME} on port 8080"
```

### Expression & Math Evaluation (`${eval:...}`)
Allows embedded arithmetic, logic, and comparisons via ExprTk:
```yaml
batch_size: 32
num_gpus: 4
total_batch: "${eval: ${batch_size} * ${num_gpus}}"  # -> 128
timeout_calc: "${eval: 10 + 5 * 2}"                 # -> 20
is_scaled: "${eval: ${batch_size} >= 64}"           # -> false
```

### Mandatory Missing Marker (`???`) & Escaped Literal (`\???`)
```yaml
api_key: ???             # Throws MissingValueMarkerException if unassigned
literal_help: \???       # Preserves string "???"
```

---

## 4. Structured C++ Configs (Structs & Glaze Binding)

OmegaConf.Cpp maps configurations directly into C++ structs with zero boilerplate using modern compile-time reflection:

```cpp
#include <string>
#include <vector>
#include <optional>

struct DatabaseConfig {
    std::string driver = "postgres";
    std::string host = "localhost";
    int port = 5432;
    int max_connections = 100;
};

struct AppConfig {
    std::string app_name;
    DatabaseConfig db;
    int workers = 4;
    std::vector<std::string> tags;
    std::optional<std::string> api_token;
};
```

Automatic casing conversion: C++ `PascalCase` or `camelCase` members automatically match YAML `snake_case` (e.g. `max_connections`), `kebab-case`, or exact names.

---

## 5. C++ Builder & CLI Integration

### Fluent Builder
```cpp
#include <omegaconf/omegaconf.hpp>

int main(int argc, char** argv) {
    AppConfig config = omegaconf::Builder<AppConfig>()
        .set_root("configs")
        .set_entry_file("config.yaml")
        .select_path("db", "production/postgres")
        .add_cli_args(argc, argv)
        .build();

    std::cout << "Connected to: " << config.db.host << ":" << config.db.port << "\n";
}
```

### CLI Overrides & Prefixes

| Prefix / Pattern | Example | Purpose / Behavior |
| :--- | :--- | :--- |
| `key=val` | `server.port=8080` | Normal override (throws in Struct Mode if key is not defined). |
| `+key=val` | `+server.ssl=true` | Strict add — throws if `server.ssl` already exists. |
| `++key=val` | `++server.ssl=true` | Upsert — safely adds if missing or updates if already present. |
| `~key` | `~server.ssl` | Deletes the key from configuration. |
| `~key=expected` | `~legacy_flag=true` | Conditional delete — only removes if current value is `true`. |
| `~group` / `~group=choice` | `~db` or `~db=postgres` | Drops default group selection entirely. |
| `tags=[a,b]` | `tags=[gpu,bench]` | Passes an explicit literal list of strings. |
| `param=v1,v2` (with `-m`) | `-m lr=0.01,0.1` | Triggers a Cartesian product sweep across 2 separate runs. |

> **Gotcha**: `++group=choice` is invalid for config groups and throws `ConfigSyntaxException`. `++` is strictly for keys/properties; config groups are selected with `group=choice` or `+group=choice`.

---

## 6. Antipatterns & Common AI Mistakes to Avoid

1. ❌ **Do NOT put operators inside scalar values**:
   - Wrong: `timeout: "+= 10"`
   - Right: `timeout+=: 10`
2. ❌ **Do NOT duplicate inherited base fields**:
   - If `db/postgres.yaml` has `port: 5432`, `db/staging/postgres.yaml` does not need to repeat `port: 5432`. It automatically cascades!
3. ❌ **Do NOT use string interpolation for pure numbers if you want strong typing**:
   - Writing `timeout: "${base_timeout}"` preserves integer type `30`. Do not wrap with extra quotes or text unless string concatenation is intended.
4. ❌ **Do NOT forget `_self_` when local file overrides are needed**:
   - If `config.yaml` defines a parameter that should take precedence over `defaults:`, always place `_self_` at the end of the `defaults:` list.
5. ❌ **Do NOT assume `group: [a, b]` produces a list**:
   - Multi-config **merges** the selected files into a single object at the group package; it never builds an array. To keep each file distinct, namespace them with `# @package group.<name>` and bind to `std::unordered_map<std::string, T>`.
6. ❌ **Do NOT use `..` parent traversal in `defaults:` lists**:
   - Wrong: `defaults: [ - ../base, - db: ../sqlite ]`
   - Right: Use absolute rooted paths prefixed with `/` (`- /shared/base`) or explicit `_inherits_: "../base.yaml"`.
7. ❌ **Do NOT redefine a default group choice in a child file without `override`**:
   - Wrong: `defaults: [ - db: mysql ]` in a child configuration when a parent base already specified a choice for `db`.
   - Right: `defaults: [ - override db: mysql ]`. Conflicting choices without `override` throw `ConfigSyntaxException`.
8. ❌ **Do NOT put `_self_` more than once in a defaults list**:
   - Duplicate `_self_` entries are invalid and throw `ConfigSyntaxException`.
9. ❌ **Do NOT confuse sweep commas with list literals in CLI overrides**:
   - In `-m` multi-run, `tags=gpu,cluster` runs 2 separate sweep runs (`tags=gpu` and `tags=cluster`).
   - To set an explicit list literal of multiple items in a single run or sweep, enclose it in brackets: `tags=[gpu,cluster]`.
10. ❌ **Do NOT use `++` on config groups in CLI**:
    - `++` is strictly for config keys/properties (e.g. `++server.timeout=30`).
    - Using `++db=mysql` throws `ConfigSyntaxException`. Use `db=mysql` or `+db=mysql` to select a group.
11. ❌ **Do NOT leave unescaped `???` when literal text is intended**:
    - `???` is the mandatory missing marker that halts execution with `MissingValueMarkerException` if not overridden before binding.
    - If you need the literal string `"???"`, escape it as `\???`.
12. ❌ **Do NOT put inline comments on the same line as `# @package`**:
    - Wrong: `# @package _global_ # load at root`
    - Right: Keep comments on separate lines above or below `# @package _global_` so the package name is parsed cleanly.
