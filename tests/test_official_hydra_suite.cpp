#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <omegaconf/omegaconf.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace omegaconf;

namespace {

struct TempDirFixture {
    fs::path temp_dir;

    TempDirFixture() {
        temp_dir = fs::temp_directory_path() / ("omegaconf_hydra_port_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        fs::create_directories(temp_dir);
    }

    ~TempDirFixture() {
        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }

    void write_file(const fs::path& rel_path, std::string_view content) {
        fs::path p = temp_dir / rel_path;
        fs::create_directories(p.parent_path());
        std::ofstream ofs(p, std::ios::binary);
        ofs << content;
    }
};

} // namespace

TEST_CASE("MissingLiterals escaped and mandatory missing behave like official Hydra", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
normal: hello
mandatory_missing: ???
escaped_literal: \???
double_escaped: \\???
)");

    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build_node();

    REQUIRE(node != nullptr);

    // 1. Mandatory missing marker (???) has is_missing_marker = true
    auto mandatory_vn = node->get_child("mandatory_missing")->as_value();
    REQUIRE(mandatory_vn != nullptr);
    CHECK(mandatory_vn->is_missing_marker());

    // 2. Escaped literal (\???) unescapes to literal string "???" and is NOT a missing marker
    auto escaped_vn = node->get_child("escaped_literal")->as_value();
    REQUIRE(escaped_vn != nullptr);
    CHECK_FALSE(escaped_vn->is_missing_marker());
    CHECK(escaped_vn->as_string() == "???");

    // 3. Double escaped (\\???) unescapes one leading backslash to "\???"
    auto double_escaped_vn = node->get_child("double_escaped")->as_value();
    REQUIRE(double_escaped_vn != nullptr);
    CHECK_FALSE(double_escaped_vn->is_missing_marker());
    CHECK(double_escaped_vn->as_string() == "\\???");
}

TEST_CASE("MissingLiterals CLI override and conditional deletion matches official Hydra", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
a: ???
b: \???
c: value
d: value
)");

    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args("~a=???", "~b=\\???", "c=???", "d=\\???")
        .build_node();

    REQUIRE(node != nullptr);
    CHECK(node->get_child("a") == nullptr);
    CHECK(node->get_child("b") == nullptr);

    auto c_node = node->get_child("c")->as_value();
    REQUIRE(c_node != nullptr);
    CHECK(c_node->is_missing_marker());

    auto d_node = node->get_child("d")->as_value();
    REQUIRE(d_node != nullptr);
    CHECK_FALSE(d_node->is_missing_marker());
    CHECK(d_node->as_string() == "???");
}

TEST_CASE("MissingLiterals conditional delete mismatch throws ConfigStructException", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
a: ???
b: \???
)");

    CHECK_THROWS_AS(
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .add_cli_arg("~a=\\???")
            .build_node(),
        ConfigStructException
    );

    CHECK_THROWS_AS(
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .add_cli_arg("~b=???")
            .build_node(),
        ConfigStructException
    );
}

TEST_CASE("OverrideKeyPaths list indexing and deletion matches Hydra", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
a:
  - 1
  - 2
)");

    SECTION("a[0]=3") {
        auto node = OmegaConfBuilder::create().set_root(fixture.temp_dir.string()).add_cli_arg("a[0]=3").build_node();
        CHECK_THAT(node->select<std::vector<int>>("a"), Catch::Matchers::Equals(std::vector<int>{3, 2}));
    }
    SECTION("a.0=3") {
        auto node = OmegaConfBuilder::create().set_root(fixture.temp_dir.string()).add_cli_arg("a.0=3").build_node();
        CHECK_THAT(node->select<std::vector<int>>("a"), Catch::Matchers::Equals(std::vector<int>{3, 2}));
    }
    SECTION("a[-1]=3") {
        auto node = OmegaConfBuilder::create().set_root(fixture.temp_dir.string()).add_cli_arg("a[-1]=3").build_node();
        CHECK_THAT(node->select<std::vector<int>>("a"), Catch::Matchers::Equals(std::vector<int>{1, 3}));
    }
    SECTION("~a[0]") {
        auto node = OmegaConfBuilder::create().set_root(fixture.temp_dir.string()).add_cli_arg("~a[0]").build_node();
        CHECK_THAT(node->select<std::vector<int>>("a"), Catch::Matchers::Equals(std::vector<int>{2}));
    }
    SECTION("~a[-1]") {
        auto node = OmegaConfBuilder::create().set_root(fixture.temp_dir.string()).add_cli_arg("~a[-1]").build_node();
        CHECK_THAT(node->select<std::vector<int>>("a"), Catch::Matchers::Equals(std::vector<int>{1}));
    }
    SECTION("~a[0]=1") {
        auto node = OmegaConfBuilder::create().set_root(fixture.temp_dir.string()).add_cli_arg("~a[0]=1").build_node();
        CHECK_THAT(node->select<std::vector<int>>("a"), Catch::Matchers::Equals(std::vector<int>{2}));
    }
}

TEST_CASE("OverrideKeyPaths nested list and bracket dict keys matches Hydra", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
matrix:
  - items: [10, 20, 30]
  - items: [40, 50, 60]
metrics:
  20s_labels: 1
  normal_key: 5
)");

    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args(
            "matrix[0].items[-1]=99",
            "~matrix[1].items[0]=40",
            "metrics[20s_labels]=42")
        .build_node();

    REQUIRE(node != nullptr);
    CHECK(node->select<int64_t>("matrix[0].items[-1]") == 99);
    CHECK(node->select<int64_t>("matrix.0.items.2") == 99);
    CHECK_THAT(node->select<std::vector<int64_t>>("matrix[1].items"), Catch::Matchers::Equals(std::vector<int64_t>{50, 60}));
    CHECK(node->select<int64_t>("metrics[20s_labels]") == 42);
}

TEST_CASE("OverrideKeyPaths conditional delete value mismatch throws", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
a: [1, 2]
b: 10
)");

    CHECK_THROWS_AS(
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .add_cli_arg("~a[0]=99")
            .build_node(),
        ConfigStructException
    );

    CHECK_THROWS_AS(
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .add_cli_arg("~b=20")
            .build_node(),
        ConfigStructException
    );
}

TEST_CASE("CliGrammarFunctions type casts sort and extend_list match Hydra", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
as_str: 10
as_int: "42"
as_float: 3
as_bool: "true"
sorted_asc: [3, 1, 2]
sorted_desc: [1, 3, 2]
tags:
  - alpha
  - beta
)");

    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args(
            "as_str=str(12345)",
            "as_int=int(99)",
            "as_float=float(2.5)",
            "as_bool=bool(false)",
            "sorted_asc=sort([30, 10, 20])",
            "sorted_desc=sort([10, 30, 20], reverse=true)",
            "tags=extend_list(gamma, delta)")
        .build_node();

    REQUIRE(node != nullptr);
    CHECK(node->get_child("as_str")->as_value()->is_string());
    CHECK(node->select<std::string>("as_str") == "12345");
    CHECK(node->select<int64_t>("as_int") == 99);
    CHECK(node->select<double>("as_float") == 2.5);
    CHECK(node->select<bool>("as_bool") == false);

    CHECK_THAT(node->select<std::vector<int64_t>>("sorted_asc"), Catch::Matchers::Equals(std::vector<int64_t>{10, 20, 30}));
    CHECK_THAT(node->select<std::vector<int64_t>>("sorted_desc"), Catch::Matchers::Equals(std::vector<int64_t>{30, 20, 10}));
    CHECK_THAT(node->select<std::vector<std::string>>("tags"), Catch::Matchers::Equals(std::vector<std::string>{"alpha", "beta", "gamma", "delta"}));
}

TEST_CASE("CliGrammarFunctions sort and shuffle in multirun sweep work", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
lr: 0.01
seed: 1
)");

    auto sweeps = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args("-m", "seed=sort(30, 10, 20, reverse=true)", "lr=shuffle(0.1, 0.2)")
        .build_sweep_nodes();

    CHECK(sweeps.size() == 6);
    CHECK(sweeps[0]->select<int64_t>("seed") == 30);
    CHECK(sweeps[2]->select<int64_t>("seed") == 20);
    CHECK(sweeps[4]->select<int64_t>("seed") == 10);
}

TEST_CASE("DefaultsList override keyword and group package placeholders match Hydra", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("db/postgres.yaml", R"(
driver: pgsql
port: 5432
)");

    fixture.write_file("db/mysql.yaml", R"(
driver: mysql
port: 3306
)");

    fixture.write_file("config.yaml", R"(
defaults:
  - /db: postgres
  - override db: mysql
  - db@_group_.replica: postgres
  - _self_
app: demo
)");

    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build_node();

    REQUIRE(node != nullptr);
    CHECK(node->select<std::string>("db.driver") == "mysql");
    CHECK(node->select<int>("db.port") == 3306);
    CHECK(node->select<std::string>("db.replica.driver") == "pgsql");
    CHECK(node->select<int>("db.replica.port") == 5432);
}

TEST_CASE("DefaultsList nested group defaults and invalid keywords match Hydra", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("cloud/aws.yaml", R"(# @package _global_
defaults:
  - db: postgres
  - _self_
region: us-east-1
)");

    fixture.write_file("db/postgres.yaml", R"(
host: rds.amazonaws.com
port: 5432
)");

    fixture.write_file("config.yaml", R"(
defaults:
  - cloud: aws
  - _self_
)");

    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build_node();

    REQUIRE(node != nullptr);
    CHECK(node->select<std::string>("region") == "us-east-1");
    CHECK(node->select<std::string>("db.host") == "rds.amazonaws.com");

    fixture.write_file("bad_config.yaml", R"(
defaults:
  - optional override db: mysql
)");

    CHECK_THROWS_AS(
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .set_entry_file("bad_config.yaml")
            .build_node(),
        ConfigSyntaxException
    );
}

TEST_CASE("SearchPaths multi-directory composition first match wins and shares groups", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("primary/config.yaml", R"(
defaults:
  - db: postgres
  - db@analytics: clickhouse
  - cache: redis
  - _self_
)");

    fixture.write_file("primary/db/postgres.yaml", "host: primary-pg.internal");
    fixture.write_file("secondary_plugin/db/postgres.yaml", "host: secondary-pg.internal");
    fixture.write_file("secondary_plugin/db/clickhouse.yaml", "host: ch.analytics.internal");
    fixture.write_file("secondary_plugin/cache/redis.yaml", "endpoint: redis:6379");

    auto node = OmegaConfBuilder::create()
        .set_root((fixture.temp_dir / "primary").string())
        .add_search_path((fixture.temp_dir / "secondary_plugin").string())
        .build_node();

    REQUIRE(node != nullptr);
    CHECK(node->select<std::string>("db.host") == "primary-pg.internal");
    CHECK(node->select<std::string>("analytics.host") == "ch.analytics.internal");
    CHECK(node->select<std::string>("cache.endpoint") == "redis:6379");
}

TEST_CASE("DefaultsList null group, missing group, multi list group, and conditional group delete match Hydra", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("db/postgres.yaml", "host: pg.local\nport: 5432");
    fixture.write_file("db/mysql.yaml", "host: mysql.local\nport: 3306");
    fixture.write_file("callbacks/checkpoint.yaml", "save_top_k: 3");
    fixture.write_file("callbacks/early_stop.yaml", "patience: 5");

    fixture.write_file("config.yaml", R"(
defaults:
  - db: postgres
  - callbacks: [checkpoint, early_stop]
  - cache: null
  - _self_
)");

    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build_node();

    REQUIRE(node != nullptr);
    CHECK(node->select<std::string>("db.host") == "pg.local");
    CHECK(node->select<int>("callbacks.save_top_k") == 3);
    CHECK(node->select<int>("callbacks.patience") == 5);
    CHECK(node->get_child("cache") == nullptr);

    // Conditional group deletion
    auto dropped_node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_arg("~db=postgres")
        .build_node();
    CHECK(dropped_node->get_child("db") == nullptr);

    // Disabling group via CLI (db=null)
    auto null_node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_arg("db=null")
        .build_node();
    CHECK(null_node->get_child("db") == nullptr);

    // Mandatory missing group
    fixture.write_file("missing_group.yaml", R"(
defaults:
  - db: ???
)");

    CHECK_THROWS_AS(
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .set_entry_file("missing_group.yaml")
            .build_node(),
        MissingValueMarkerException
    );

    auto resolved_missing = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .set_entry_file("missing_group.yaml")
        .add_cli_arg("db=mysql")
        .build_node();
    CHECK(resolved_missing->select<std::string>("db.host") == "mysql.local");
}

TEST_CASE("DefaultsList duplicate self and force add group throw", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("db/postgres.yaml", "port: 5432");

    fixture.write_file("dup_self.yaml", R"(
defaults:
  - _self_
  - db: postgres
  - _self_
)");

    CHECK_THROWS_AS(
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .set_entry_file("dup_self.yaml")
            .build_node(),
        ConfigSyntaxException
    );

    fixture.write_file("valid.yaml", "app: test");
    CHECK_THROWS_AS(
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .set_entry_file("valid.yaml")
            .add_cli_arg("++db=mysql")
            .build_node(),
        ConfigSyntaxException
    );
}

TEST_CASE("GrammarFunctions glob exclude and single arg range match Hydra", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("db/postgres.yaml", "driver: pg");
    fixture.write_file("db/mysql.yaml", "driver: mysql");
    fixture.write_file("db/sqlite.yaml", "driver: sqlite");

    fixture.write_file("config.yaml", R"(
defaults:
  - db: postgres
  - _self_
idx: 0
lr: 0.1
items: []
flag_str: ""
)");

    auto sweeps = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args(
            "-m",
            "db=glob(*, exclude=sqlite)",
            "idx=range(2)",
            "lr=tag(fast_sweep, sort(choice(0.2, 0.1)))")
        .build_sweep_nodes();

    CHECK(sweeps.size() == 8);
    for (const auto& s : sweeps) {
        CHECK(s->select<std::string>("db.driver") != "sqlite");
    }

    auto casted = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args("flag_str=str(true)")
        .build_node();
    CHECK(casted->select<std::string>("flag_str") == "true");
}

TEST_CASE("GroupPackage CLI delete and override with symbolic group keyword matches Hydra", "[official_hydra]") {
    TempDirFixture fixture;

    fixture.write_file("preprocess/v1.yaml", "step: normalize");
    fixture.write_file("preprocess/v2.yaml", "step: tokenize");
    fixture.write_file("cloud/aws/rds.yaml", "engine: aurora");
    fixture.write_file("item.yaml", "x: 42");

    fixture.write_file("config.yaml", R"(
defaults:
  - /preprocess@_group_: v1
  - /cloud/aws@_group_.primary: rds
  - /item@_group_.suffix
  - _self_
)");

    auto base_node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build_node();

    REQUIRE(base_node != nullptr);
    CHECK(base_node->select<std::string>("preprocess.step") == "normalize");
    CHECK(base_node->select<std::string>("cloud.aws.primary.engine") == "aurora");
    CHECK(base_node->select<int>("suffix.x") == 42);

    auto overridden = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_arg("preprocess=v2")
        .build_node();
    CHECK(overridden->select<std::string>("preprocess.step") == "tokenize");

    auto deleted = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .with_struct_mode()
        .add_cli_arg("~preprocess@_group_")
        .build_node();
    CHECK(deleted->get_child("preprocess") == nullptr);
}

TEST_CASE("Relative dot navigation inside list elements", "[official_hydra]") {
    auto list_interp_cfg = OmegaConfig::parse_yaml(R"(
parent_prefix: global_v1
container:
  sibling_tag: local_tag
  items:
    - first_elem
    - "${.0}:${..sibling_tag}:${...parent_prefix}"
)");

    auto merged = OmegaConfig::merge(list_interp_cfg);
    CHECK(merged->select<std::string>("container.items[1]") == "first_elem:local_tag:global_v1");
}
