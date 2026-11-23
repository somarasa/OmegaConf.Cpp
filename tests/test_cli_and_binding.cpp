#include <catch2/catch_test_macros.hpp>
#include <omegaconf/omegaconf.hpp>

#include <filesystem>
#include <fstream>
#include <optional>

namespace fs = std::filesystem;
using namespace omegaconf;

struct AppDatabaseConfig {
    std::string driver{};
    std::string host{};
    int port{0};
    int max_connections{0};
    bool ssl{false};
    int timeout_seconds{0};
};
OMEGACONF_REFLECT(AppDatabaseConfig, driver, host, port, max_connections, ssl, timeout_seconds)

struct AppConfig {
    std::string app_name{};
    std::optional<AppDatabaseConfig> db{};
    int active_workers{4};
    std::string internal_secret{"do-not-overwrite"};
};
OMEGACONF_REFLECT(AppConfig, app_name, db, active_workers, internal_secret)

struct CliTempDirFixture {
    fs::path temp_dir;

    CliTempDirFixture() {
        temp_dir = fs::temp_directory_path() / ("omegaconf_cli_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        fs::create_directories(temp_dir);
    }

    ~CliTempDirFixture() {
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

TEST_CASE("CliOverrides should modify paths and deep values with operators and bind to struct", "[cli][binding]") {
    CliTempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
defaults:
  - db: default
app_name: MyBaseApp
active_workers: 2
debug_mode: false
)");

    fixture.write_file("db/default.yaml", R"(
driver: sqlite
port: 3306
timeout_seconds: 10
)");

    std::vector<std::string> args = {
        "app_name=OverrideAppName",
        "active_workers+=4",
        "db.timeout_seconds+=5",
        "debug_mode!=true"
    };

    auto config = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args(args)
        .build<AppConfig>();

    CHECK(config.app_name == "OverrideAppName");
    CHECK(config.active_workers == 6); // 2 + 4
    REQUIRE(config.db.has_value());
    CHECK(config.db->timeout_seconds == 15); // 10 + 5
}

TEST_CASE("DropGroup should exclude default from composed tree", "[cli]") {
    CliTempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
defaults:
  - db: default
app_name: MyApp
)");

    fixture.write_file("db/default.yaml", R"(
driver: postgres
port: 5432
)");

    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_arg("~db")
        .build_node();

    REQUIRE(node != nullptr);
    CHECK(node->get_child("app_name")->as_value()->as_string() == "MyApp");
    CHECK(node->get_child("db") == nullptr);
}
