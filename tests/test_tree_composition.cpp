#include <catch2/catch_test_macros.hpp>
#include <omegaconf/omegaconf.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace omegaconf;

struct TempDirFixture {
    fs::path temp_dir;

    TempDirFixture() {
        temp_dir = fs::temp_directory_path() / ("omegaconf_tree_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
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

TEST_CASE("TreeResolver should support cascading and operators", "[composition]") {
    TempDirFixture fixture;

    // Entrypoint config.yaml
    fixture.write_file("config.yaml", R"(
defaults:
  - db: staging/postgres
  - _self_

app_name: MyService
)");

    // Base postgres
    fixture.write_file("db/postgres.yaml", R"(
driver: postgres
port: 5432
user: db_admin
max_connections: 50
ssl: false
timeout_seconds: 30
)");

    // Staging postgres (overrides host, reduces connections)
    fixture.write_file("db/staging/postgres.yaml", R"(
host: staging-db.internal
max_connections: 20
)");

    // Production postgres (uses operators: *= and +=)
    fixture.write_file("db/production/postgres.yaml", R"(
host: prod-db.internal
max_connections*=: 4
ssl: true
timeout_seconds+=: 30
)");

    // TEST 1: Default selection (staging/postgres)
    auto staging_config = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build_node();

    REQUIRE(staging_config != nullptr);
    CHECK(staging_config->get_child("app_name")->as_value()->as_string() == "MyService");

    auto db_node = staging_config->get_child("db");
    REQUIRE(db_node != nullptr);
    REQUIRE(db_node->is_object());
    auto* db_obj = db_node->as_object();

    CHECK(db_obj->get_child("driver")->as_value()->as_string() == "postgres"); // from base
    CHECK(db_obj->get_child("port")->as_value()->as_integer() == 5432);        // from base
    CHECK(db_obj->get_child("user")->as_value()->as_string() == "db_admin");   // from base
    CHECK(db_obj->get_child("host")->as_value()->as_string() == "staging-db.internal"); // from staging
    CHECK(db_obj->get_child("max_connections")->as_value()->as_integer() == 20); // overridden
    CHECK(db_obj->get_child("ssl")->as_value()->as_bool() == false);           // from base
    CHECK(db_obj->get_child("timeout_seconds")->as_value()->as_integer() == 30); // from base

    // TEST 2: Select production path (production/postgres)
    auto prod_config = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .select_path("db", "production/postgres")
        .build_node();

    REQUIRE(prod_config != nullptr);
    auto prod_db = prod_config->get_child("db")->as_object();
    CHECK(prod_db->get_child("host")->as_value()->as_string() == "prod-db.internal");
    CHECK(prod_db->get_child("max_connections")->as_value()->as_integer() == 200); // 50 * 4
    CHECK(prod_db->get_child("ssl")->as_value()->as_bool() == true);
    CHECK(prod_db->get_child("timeout_seconds")->as_value()->as_integer() == 60);  // 30 + 30
}

TEST_CASE("ExplicitInheritance should load base file", "[composition]") {
    TempDirFixture fixture;

    fixture.write_file("explicit/base.yaml", R"(
base_param: original
shared_metric: 100
)");

    fixture.write_file("explicit/child.yaml", R"(
_inherits_: base.yaml
child_param: custom
shared_metric+=: 50
)");

    auto result = OmegaConfBuilder::create()
        .set_root((fixture.temp_dir / "explicit").string())
        .set_entry_file("child.yaml")
        .build_node();

    REQUIRE(result != nullptr);
    CHECK(result->get_child("base_param")->as_value()->as_string() == "original");
    CHECK(result->get_child("child_param")->as_value()->as_string() == "custom");
    CHECK(result->get_child("shared_metric")->as_value()->as_integer() == 150);
}
