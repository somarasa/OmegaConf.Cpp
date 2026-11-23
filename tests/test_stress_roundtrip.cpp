#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "omegaconf/omegaconf.hpp"

#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <vector>

using namespace omegaconf;
namespace fs = std::filesystem;

namespace stress_test {

struct AppDatabaseConfig {
    std::string driver{};
    std::string host{};
    int port{0};
    int max_connections{0};
    int timeout_seconds{0};
};
OMEGACONF_REFLECT(AppDatabaseConfig, driver, host, port, max_connections, timeout_seconds)

struct AppConfig {
    std::string app_name{};
    int active_workers{0};
    std::optional<AppDatabaseConfig> db{};
};
OMEGACONF_REFLECT(AppConfig, app_name, active_workers, db)

} // namespace stress_test

using namespace stress_test;

namespace {

struct StressTempDirFixture {
    fs::path temp_dir;

    StressTempDirFixture() {
        auto base = fs::temp_directory_path();
        auto rand_id = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        temp_dir = base / ("omegaconf_stress_" + rand_id);
        fs::create_directories(temp_dir);
    }

    ~StressTempDirFixture() {
        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }

    void write_file(const std::string& rel_path, const std::string& content) {
        fs::path p = temp_dir / rel_path;
        fs::create_directories(p.parent_path());
        std::ofstream ofs(p);
        ofs << content;
    }
};

} // namespace

// =========================================================================
// 1. Parameterized Theory: ToSnakeCase
// =========================================================================

TEST_CASE("ParameterizedTheory ToSnakeCase converts acronyms and PascalCase accurately", "[stress][binding]") {
    CHECK(ConfigBinder::to_snake_case("AppName") == "app_name");
    CHECK(ConfigBinder::to_snake_case("SSLPort") == "ssl_port");
    CHECK(ConfigBinder::to_snake_case("HTTPSEndpoint") == "https_endpoint");
    CHECK(ConfigBinder::to_snake_case("MaxDBConnections") == "max_db_connections");
    CHECK(ConfigBinder::to_snake_case("Simple") == "simple");
    CHECK(ConfigBinder::to_snake_case("Already_Snake_Case") == "already_snake_case");
    CHECK(ConfigBinder::to_snake_case("OAuth2ClientID") == "o_auth2_client_id");
}

// =========================================================================
// 2. Parameterized Theory: EvalExpressions
// =========================================================================

TEST_CASE("ParameterizedTheory EvalExpressions evaluates all operators correctly", "[stress][eval]") {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"${eval: 10 + 20 * 3}", "70"},
        {"${eval: (10 + 20) * 3}", "90"},
        {"${eval: 100 / 4 - 5}", "20"},
        {"${eval: 15 % 4}", "3"},
        {"${eval: -10 + 25}", "15"},
        {"${eval: true && !false}", "True"},
        {"${eval: 5 == 5 && 3 != 4}", "True"},
        {"${eval: 10 > 20 || 5 <= 5}", "True"},
        {"${eval: 'hello' == 'hello'}", "True"}
    };

    for (const auto& [expr, expected] : cases) {
        auto root = std::make_shared<ConfigObjectNode>();
        root->set_child("result", std::make_shared<ConfigValueNode>(expr));

        InterpolationEngine engine(root);
        engine.resolve_all();

        CHECK(root->get_child("result")->as_value()->to_string() == expected);
    }
}

// =========================================================================
// 3. Round-Trip Serialization: YAML and JSON
// =========================================================================

TEST_CASE("RoundTripSerialization Yaml and Json preserves tree equivalence", "[stress][serialization]") {
    auto original = std::make_shared<ConfigObjectNode>();
    original->set_child("service_name", std::make_shared<ConfigValueNode>("OrderProcessor"));
    original->set_child("max_workers", std::make_shared<ConfigValueNode>(16LL));
    original->set_child("enable_metrics", std::make_shared<ConfigValueNode>(true));
    original->set_child("threshold", std::make_shared<ConfigValueNode>(0.85));

    auto tags = std::make_shared<ConfigListNode>();
    tags->add_item(std::make_shared<ConfigValueNode>("production"));
    tags->add_item(std::make_shared<ConfigValueNode>("eu-central"));
    tags->add_item(std::make_shared<ConfigValueNode>("critical"));
    original->set_child("tags", tags);

    auto db = std::make_shared<ConfigObjectNode>();
    db->set_child("host", std::make_shared<ConfigValueNode>("orders-db.internal"));
    db->set_child("port", std::make_shared<ConfigValueNode>(5432LL));
    original->set_child("database", db);

    // 1. Round-trip through YAML
    std::string yaml = original->to_yaml();
    auto from_yaml = YamlFormatParser::parse(yaml);
    auto diff_yaml = ConfigDiffer::compare(original, from_yaml);
    CHECK_FALSE(diff_yaml.has_changes());

    // 2. Round-trip through JSON
    std::string json = original->to_json(true);
    auto from_json = YamlFormatParser::parse(json);
    auto diff_json = ConfigDiffer::compare(original, from_json);
    CHECK_FALSE(diff_json.has_changes());
}

// =========================================================================
// 4. AST Deep Clone and Operator Merge Immutability
// =========================================================================

TEST_CASE("AstDeepClone and Operator merge never mutates original nodes", "[stress][ast]") {
    auto base_tree = std::make_shared<ConfigObjectNode>();
    auto base_db = std::make_shared<ConfigObjectNode>();
    base_db->set_child("port", std::make_shared<ConfigValueNode>(5432LL));

    auto base_roles = std::make_shared<ConfigListNode>();
    base_roles->add_item(std::make_shared<ConfigValueNode>("reader"));
    base_db->set_child("roles", base_roles);
    base_tree->set_child("db", base_db);

    auto override_tree = std::make_shared<ConfigObjectNode>();
    auto override_db = std::make_shared<ConfigObjectNode>();
    override_db->set_child("port", std::make_shared<ConfigValueNode>(1000LL, ConfigOperator::Add));

    auto override_roles = std::make_shared<ConfigListNode>(ConfigOperator::Add);
    override_roles->add_item(std::make_shared<ConfigValueNode>("writer"));
    override_db->set_child("roles", override_roles);
    override_tree->set_child("db", override_db);

    // Apply merge
    auto merged = std::static_pointer_cast<ConfigObjectNode>(OperatorEngine::apply(base_tree, override_tree));

    // Mutate merged tree afterwards
    auto merged_db = std::static_pointer_cast<ConfigObjectNode>(merged->get_child("db"));
    merged_db->set_child("port", std::make_shared<ConfigValueNode>(9999LL));
    merged_db->get_child("roles")->as_list()->add_item(std::make_shared<ConfigValueNode>("admin"));

    // Verify base_tree and override_tree were completely untouched
    CHECK(base_db->get_child("port")->as_value()->as_integer() == 5432LL);
    CHECK(base_db->get_child("roles")->as_list()->size() == 1);
    CHECK((*base_db->get_child("roles")->as_list())[0]->to_string() == "reader");

    CHECK(override_db->get_child("port")->as_value()->as_integer() == 1000LL);
    CHECK(override_db->get_child("roles")->as_list()->size() == 1);
    CHECK((*override_db->get_child("roles")->as_list())[0]->to_string() == "writer");
}

// =========================================================================
// 5. Concurrent Parallel Builds
// =========================================================================

TEST_CASE("ConcurrentParallelBuilds thread safe without race conditions", "[stress][threading]") {
    StressTempDirFixture fixture;

    fixture.write_file("config.yaml", R"(
defaults:
  - db: postgres
  - _self_
app_name: ConcurrentService
active_workers: 4
)");

    fixture.write_file("db/postgres.yaml", R"(
driver: postgres
host: db.local
port: 5432
max_connections: 25
timeout_seconds: 10
)");

    constexpr int kNumTasks = 30;
    std::vector<std::future<std::pair<int, AppConfig>>> futures;
    futures.reserve(kNumTasks);

    for (int i = 1; i <= kNumTasks; ++i) {
        futures.push_back(std::async(std::launch::async, [&fixture, i]() {
            auto cfg = OmegaConfBuilder::create()
                .set_root(fixture.temp_dir.string())
                .add_cli_arg("active_workers+=" + std::to_string(i))
                .add_cli_arg("db.max_connections*=" + std::to_string(i))
                .build<AppConfig>();
            return std::make_pair(i, cfg);
        }));
    }

    for (auto& fut : futures) {
        auto [i, cfg] = fut.get();
        CHECK(cfg.app_name == "ConcurrentService");
        CHECK(cfg.active_workers == 4 + i);
        REQUIRE(cfg.db.has_value());
        CHECK(cfg.db->max_connections == 25 * i);
        CHECK(cfg.db->driver == "postgres");
    }
}

// =========================================================================
// 6. Malformed Syntax and Edge Case Fuzzing
// =========================================================================

TEST_CASE("MalformedSyntaxAndEdgeCaseFuzzing throws structured OmegaConf exceptions", "[stress][exceptions]") {
    // 1. Malformed YAML (unclosed bracket)
    CHECK_THROWS_AS(YamlFormatParser::parse("key: [unclosed, list", "broken.yaml"), ConfigSyntaxException);

    // 2. Malformed JSON (non-object root)
    try {
        YamlFormatParser::parse("[1, 2, 3]", "array_root.json");
        FAIL("Expected ConfigSyntaxException for non-object root");
    } catch (const ConfigSyntaxException& ex) {
        CHECK_THAT(ex.what(), Catch::Matchers::ContainsSubstring("must be a mapping / object"));
    }

    // 3. Malformed eval expression (unclosed parenthesis)
    auto root = std::make_shared<ConfigObjectNode>();
    root->set_child("broken_eval", std::make_shared<ConfigValueNode>("${eval: (10 + 5}"));
    InterpolationEngine engine(root);
    CHECK_THROWS_AS(engine.resolve_all(), ConfigSyntaxException);
}
