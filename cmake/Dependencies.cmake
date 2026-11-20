include(FetchContent)

# 1. RapidYAML (ryml) for ultra-fast YAML parsing
FetchContent_Declare(
    ryml
    GIT_REPOSITORY https://github.com/biojppm/rapidyaml.git
    GIT_TAG        v0.7.2
    GIT_SHALLOW    TRUE
)
set(RYML_BUILD_TESTS OFF CACHE INTERNAL "")
set(RYML_BUILD_BENCHMARKS OFF CACHE INTERNAL "")

# 2. Glaze for zero-cost compile-time struct reflection & JSON/YAML/TOML binding
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG        v4.2.4
    GIT_SHALLOW    TRUE
)

# 3. ExprTk for ${eval:...} mathematical, logical & string expression evaluation
FetchContent_Declare(
    exprtk
    GIT_REPOSITORY https://github.com/ArashPartow/exprtk.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# 4. Catch2 v3 for comprehensive unit testing
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(ryml glaze Catch2 exprtk)
