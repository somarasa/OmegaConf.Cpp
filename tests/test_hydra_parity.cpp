#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <omegaconf/omegaconf.hpp>

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <chrono>

namespace fs = std::filesystem;
using namespace omegaconf;

namespace {

struct TempDirFixture {
    fs::path temp_dir;

    TempDirFixture() {
        temp_dir = fs::temp_directory_path() / ("omegaconf_parity_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
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

TEST_CASE("PackageDirectives should mount multiple groups at custom packages and support global package and file headers", "[parity]") {
    TempDirFixture fixture;

    fixture.write_file("db/postgres.yaml", R"(
driver: postgresql
port: 5432
)");

    fixture.write_file("db/sqlite.yaml", R"(
driver: sqlite
port: 0
)");

    // File with # @package _global_ header
    fixture.write_file("experiment/fast_train.yaml", R"(# @package _global_
workers: 16
primary_db:
  port: 6432
)");

    fixture.write_file("config.yaml", R"(
defaults:
  - db@primary_db: postgres
  - db@replica_db: sqlite
  - _self_

workers: 4
)");

    auto root = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_arg("+experiment=fast_train")
        .build_node();

    REQUIRE(root != nullptr);
    CHECK(root->select<std::string>("primary_db.driver") == "postgresql");
    CHECK(root->select<int>("primary_db.port") == 6432); // Overridden by fast_train @_global_!
    CHECK(root->select<std::string>("replica_db.driver") == "sqlite");
    CHECK(root->select<int>("workers") == 16);
}

TEST_CASE("SweepFunctions range, linspace, choice, and glob should expand correctly", "[parity]") {
    TempDirFixture fixture;

    fixture.write_file("db/postgres.yaml", "driver: pg\nport: 5432");
    fixture.write_file("db/sqlite.yaml", "driver: sqlite\nport: 0");
    fixture.write_file("db/mysql.yaml", "driver: mysql\nport: 3306");

    fixture.write_file("config.yaml", R"(
defaults:
  - db: sqlite
  - _self_
workers: 1
lr: 0.1
)");

    // Verify wildcard filtering with glob(*sql*) -> matches mysql and sqlite (excludes postgres)
    auto filter_res = SweepEngine::parse_sweep_args({"-m", "db=glob(*sql*)"}, fixture.temp_dir.string());
    REQUIRE(filter_res.sweep_matrix.find("db") != filter_res.sweep_matrix.end());
    CHECK_THAT(filter_res.sweep_matrix["db"], Catch::Matchers::Equals(std::vector<std::string>{"mysql", "sqlite"}));

    std::vector<std::string> args = {
        "-m",
        "db=glob(*)",              // matches mysql, postgres, sqlite (3)
        "workers=range(2, 6, 2)",  // 2, 4 (2)
        "lr=linspace(0.1, 0.2, 2)" // 0.1, 0.2 (2)
    };

    auto sweep_res = SweepEngine::parse_sweep_args(args, fixture.temp_dir.string());
    CHECK(sweep_res.non_sweep_args.empty());
    CHECK_THAT(sweep_res.sweep_matrix["db"], Catch::Matchers::Equals(std::vector<std::string>{"mysql", "postgres", "sqlite"}));
    CHECK_THAT(sweep_res.sweep_matrix["workers"], Catch::Matchers::Equals(std::vector<std::string>{"2", "4"}));
    CHECK(sweep_res.sweep_matrix["lr"].size() == 2);

    auto sweep_nodes = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args(args)
        .build_sweep_nodes();

    // 3 * 2 * 2 = 12 combinations
    CHECK(sweep_nodes.size() == 12);
}

TEST_CASE("CliDeepKeyDeletion and StrictStructMode with plus and plus-plus modifiers should work", "[parity]") {
    TempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
app_name: StrictApp
db:
  host: localhost
  port: 5432
  secret_token: s3cr3t
)");

    // 1. Typo on undeclared key in Struct Mode should throw ConfigStructException
    CHECK_THROWS_AS(
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .with_struct_mode()
            .add_cli_arg("db.prot=5433")
            .build_node(),
        ConfigStructException
    );

    // 2. +key on an ALREADY existing key in Struct Mode should throw ConfigStructException
    CHECK_THROWS_AS(
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .with_struct_mode()
            .add_cli_arg("+db.port=5433")
            .build_node(),
        ConfigStructException
    );

    // 3. ~db.secret_token, +db.pool_size=20, ++db.port=5433, ++db.ssl=true should succeed!
    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .with_struct_mode()
        .add_cli_args("~db.secret_token", "+db.pool_size=20", "++db.port=5433", "++db.ssl=true")
        .build_node();

    REQUIRE(node != nullptr);
    CHECK(node->select_node("db.secret_token") == nullptr);
    CHECK(node->select<int>("db.pool_size") == 20);
    CHECK(node->select<int>("db.port") == 5433);
    CHECK(node->select<bool>("db.ssl") == true);
}

TEST_CASE("BuiltInOcResolvers oc.select, oc.decode, oc.dict.keys, oc.env, and now should work", "[parity]") {
    TempDirFixture fixture;

#if defined(_MSC_VER)
    _putenv_s("OC_TEST_DECODE_PORT", "9090");
#else
    setenv("OC_TEST_DECODE_PORT", "9090", 1);
#endif

    fixture.write_file("config.yaml", R"(
db:
  host: localhost
  port: 5432
selected_existing: "${oc.select:db.port, 3306}"
selected_missing_with_fallback: "${oc.select:db.missing_timeout, 45}"
selected_missing_null: "${oc.select:db.non_existent}"
decoded_env_port: "${oc.decode:${oc.env:OC_TEST_DECODE_PORT}}"
decoded_list: "${oc.decode:[10, 20, 30]}"
db_keys: "${oc.dict.keys:db}"
current_year: "${now:yyyy}"
)");

    auto root = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build_node();

    REQUIRE(root != nullptr);
    CHECK(root->select<int64_t>("selected_existing") == 5432);
    CHECK(root->select<int64_t>("selected_missing_with_fallback") == 45);
    CHECK(root->select_node("selected_missing_null")->as_value()->is_null());
    CHECK(root->select<int64_t>("decoded_env_port") == 9090);
    CHECK_THAT(root->select<std::vector<int>>("decoded_list"), Catch::Matchers::Equals(std::vector<int>{10, 20, 30}));
    CHECK_THAT(root->select<std::vector<std::string>>("db_keys"), Catch::Matchers::Equals(std::vector<std::string>{"host", "port"}));

    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_MSC_VER)
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    std::string expected_year = std::to_string(tm_buf.tm_year + 1900);
    CHECK(root->select<std::string>("current_year") == expected_year);
}

TEST_CASE("ProgrammaticOmegaConfFacade select, merge, set_readonly, and set_struct should work", "[parity]") {
    auto base_cfg = OmegaConfig::parse_yaml(R"(
server:
  host: 127.0.0.1
  port: 8080
  endpoints:
    - /health
    - /metrics
)");

    auto override_cfg = OmegaConfig::parse_json(R"({
  "server": {
    "port+=": 10,
    "url": "http://${server.host}:${server.port}${server.endpoints[0]}"
  }
})");

    auto merged = OmegaConfig::merge(base_cfg, override_cfg);

    CHECK(OmegaConfig::select<int>(merged, "server.port") == 8090);
    CHECK(OmegaConfig::select<std::string>(merged, "server.url") == "http://127.0.0.1:8090/health");
    CHECK(OmegaConfig::select<std::string>(merged, "server.missing", "default_val") == "default_val");

    // Test set_struct
    OmegaConfig::set_struct(merged, true);
    CHECK_THROWS_AS(merged->set_child("undeclared", std::make_shared<ConfigValueNode>(1)), ConfigStructException);

    // Test set_read_only / freeze
    OmegaConfig::set_read_only(merged, true);
    CHECK(merged->is_read_only());
    CHECK_THROWS_AS(merged->select_node("server.port")->as_value()->set_value(9999LL), ConfigReadOnlyException);
}

TEST_CASE("OutputDirectorySnapshot should write reproducible artifacts for single run and multi run", "[parity]") {
    TempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
app_name: SnapshotApp
workers: 2
)");

    std::string single_run_out = (fixture.temp_dir / "outputs" / "single").string();
    auto single_node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_arg("workers=8")
        .with_output_directory(single_run_out)
        .build_node();

    REQUIRE(single_node != nullptr);
    CHECK(fs::exists(fs::path(single_run_out) / ".omegaconf" / "config.yaml"));
    CHECK(fs::exists(fs::path(single_run_out) / ".omegaconf" / "overrides.yaml"));

    std::string sweep_out = (fixture.temp_dir / "outputs" / "multirun").string();
    auto sweep_nodes = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args("-m", "workers=range(2, 5)")
        .with_output_directory(sweep_out)
        .build_sweep_nodes();

    CHECK(sweep_nodes.size() == 3);
    CHECK(fs::exists(fs::path(sweep_out) / "multirun.yaml"));
    CHECK(fs::exists(fs::path(sweep_out) / "0" / ".omegaconf" / "config.yaml"));
    CHECK(fs::exists(fs::path(sweep_out) / "1" / ".omegaconf" / "config.yaml"));
    CHECK(fs::exists(fs::path(sweep_out) / "2" / ".omegaconf" / "config.yaml"));
}
