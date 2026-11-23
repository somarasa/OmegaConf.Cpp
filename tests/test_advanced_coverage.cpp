#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include "omegaconf/omegaconf.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

using namespace omegaconf;
namespace fs = std::filesystem;

namespace adv_test {

struct RichTypesConfig {
    std::string tier{};
    std::optional<int> nullable_int{};
    std::string cache_duration{};
    std::string correlation_id{};
    std::vector<std::string> endpoints{};
    std::vector<int> ports{};
    std::unordered_map<std::string, std::string> headers{};
};
OMEGACONF_REFLECT(RichTypesConfig, tier, nullable_int, cache_duration, correlation_id, endpoints, ports, headers)

struct ServiceConfig {
    std::string service_name{};
    int max_retries{3};
    int ssl_port{443};
};
OMEGACONF_REFLECT(ServiceConfig, service_name, max_retries, ssl_port)

struct AppDatabaseConfig {
    std::string driver{};
    int port{0};
};
OMEGACONF_REFLECT(AppDatabaseConfig, driver, port)

struct AppConfig {
    std::string app_name{};
    std::optional<AppDatabaseConfig> db{};
};
OMEGACONF_REFLECT(AppConfig, app_name, db)

struct MemoryCacheService {
    std::string _target_{};
    std::string prefix{"default"};
    [[nodiscard]] std::string get_name() const { return "Memory:" + prefix; }
};
OMEGACONF_REFLECT(MemoryCacheService, _target_, prefix)

struct DynamicTargetConfig {
    std::optional<MemoryCacheService> cache{};
};
OMEGACONF_REFLECT(DynamicTargetConfig, cache)

struct ExtendedTypesConfig {
    std::string endpoint_uri{};
    std::string created_at{};
    std::string release_date{};
    std::string maintenance_window{};
    int ssl_port{0};
    std::string https_endpoint{};
};
OMEGACONF_REFLECT(ExtendedTypesConfig, endpoint_uri, created_at, release_date, maintenance_window, ssl_port, https_endpoint)

class SlugifyResolver : public IResolver {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "slugify"; }
    [[nodiscard]] std::string resolve(std::string_view argument, IInterpolationContext&) override {
        std::string s(argument);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return std::tolower(c) == ' ' ? '-' : static_cast<char>(std::tolower(c));
        });
        return s;
    }
};

} // namespace adv_test

using namespace adv_test;

namespace {

struct AdvancedTempFixture {
    fs::path temp_dir;

    AdvancedTempFixture() {
        auto base = fs::temp_directory_path();
        auto rand_id = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        temp_dir = base / ("omegaconf_cov_adv_" + rand_id);
        fs::create_directories(temp_dir);
    }

    ~AdvancedTempFixture() {
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
// 1. StructuredBinding Rich Types
// =========================================================================

TEST_CASE("StructuredBinding supports strings, nullables, collections and dictionaries", "[advanced][binding]") {
    AdvancedTempFixture fixture;

    fixture.write_file("config.yaml", R"(
tier: Staging
nullable_int: 42
cache_duration: "00:15:00"
correlation_id: "11111111-2222-3333-4444-555555555555"
endpoints:
  - "https://api1.local"
  - "https://api2.local"
ports:
  - 80
  - 443
headers:
  X-Custom-Env: Test
  Authorization: Bearer xyz
)");

    auto cfg = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build<RichTypesConfig>();

    CHECK(cfg.tier == "Staging");
    REQUIRE(cfg.nullable_int.has_value());
    CHECK(*cfg.nullable_int == 42);
    CHECK(cfg.cache_duration == "00:15:00");
    CHECK(cfg.correlation_id == "11111111-2222-3333-4444-555555555555");
    REQUIRE(cfg.endpoints.size() == 2);
    CHECK(cfg.endpoints[0] == "https://api1.local");
    CHECK(cfg.endpoints[1] == "https://api2.local");
    REQUIRE(cfg.ports.size() == 2);
    CHECK(cfg.ports[0] == 80);
    CHECK(cfg.ports[1] == 443);
    CHECK(cfg.headers["X-Custom-Env"] == "Test");
    CHECK(cfg.headers["Authorization"] == "Bearer xyz");
}

// =========================================================================
// 2. Optional vs Non-Optional Default Files Missing
// =========================================================================

TEST_CASE("OptionalDefault does not throw when file is missing", "[advanced][defaults]") {
    AdvancedTempFixture fixture;

    fixture.write_file("config.yaml", R"(
defaults:
  - optional non_existent_group: missing_file
  - _self_
app_name: SafeApp
)");

    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build_node();

    REQUIRE(node != nullptr);
    REQUIRE(node->get_child("app_name") != nullptr);
    CHECK(node->get_child("app_name")->as_value()->as_string() == "SafeApp");
}

TEST_CASE("NonOptionalDefault throws when file is missing", "[advanced][defaults]") {
    AdvancedTempFixture fixture;

    fixture.write_file("config.yaml", R"(
defaults:
  - mandatory_group: missing_file
)");

    CHECK_THROWS_AS(OmegaConfBuilder::create()
                        .set_root(fixture.temp_dir.string())
                        .build_node(),
                    ConfigInheritanceException);
}

// =========================================================================
// 3. Circular Inheritance Detection
// =========================================================================

TEST_CASE("CircularInheritance throws ConfigInheritanceException", "[advanced][inheritance]") {
    AdvancedTempFixture fixture;

    fixture.write_file("fileA.yaml", R"(
_inherits_: fileB.yaml
a: 1
)");

    fixture.write_file("fileB.yaml", R"(
_inherits_: fileA.yaml
b: 2
)");

    try {
        OmegaConfBuilder::create()
            .set_root(fixture.temp_dir.string())
            .set_entry_file("fileA.yaml")
            .build_node();
        FAIL("Expected ConfigInheritanceException for circular inheritance");
    } catch (const ConfigInheritanceException& ex) {
        CHECK_THAT(ex.what(), Catch::Matchers::ContainsSubstring("Circular"));
    }
}

// =========================================================================
// 4. _self_ Placement Precedence
// =========================================================================

TEST_CASE("SelfPlacement determines precedence over defaults", "[advanced][defaults]") {
    AdvancedTempFixture fixture;

    fixture.write_file("sub/setting.yaml", "val: from_default\n");

    // Case 1: _self_ after defaults (local file wins)
    fixture.write_file("self_wins.yaml", R"(
defaults:
  - sub: setting
  - _self_
val: from_local
)");

    auto node1 = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .set_entry_file("self_wins.yaml")
        .build_node();

    REQUIRE(node1->get_child("val") != nullptr);
    CHECK(node1->get_child("val")->as_value()->as_string() == "from_local");

    // Case 2: _self_ before defaults (defaults win if at root level or mounted)
    fixture.write_file("defaults_win.yaml", R"(
defaults:
  - _self_
  - sub: setting
val: from_local
)");

    auto node2 = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .set_entry_file("defaults_win.yaml")
        .build_node();

    CHECK(node2->get_child("sub") != nullptr);
}

// =========================================================================
// 5. Bitwise Operators on Integers and Divide by Zero
// =========================================================================

TEST_CASE("BitwiseOperators on integers and divide by zero validation", "[advanced][operators]") {
    // 12 (1100) & 10 (1010) = 8 (1000)
    auto base_int = std::make_shared<ConfigValueNode>(12LL);
    auto and_int = std::make_shared<ConfigValueNode>(10LL, ConfigOperator::BitwiseAnd);
    auto res_and = OperatorEngine::apply(base_int, and_int);
    CHECK(res_and->as_value()->as_integer() == 8LL);

    // 12 (1100) | 3 (0011) = 15 (1111)
    auto or_int = std::make_shared<ConfigValueNode>(3LL, ConfigOperator::BitwiseOr);
    auto res_or = OperatorEngine::apply(base_int, or_int);
    CHECK(res_or->as_value()->as_integer() == 15LL);

    // Divide by zero
    auto zero = std::make_shared<ConfigValueNode>(0LL, ConfigOperator::Divide);
    CHECK_THROWS_AS(OperatorEngine::apply(base_int, zero), ConfigDivideByZeroException);
}

// =========================================================================
// 6. Advanced Eval Expressions
// =========================================================================

TEST_CASE("AdvancedEvalExpressions parentheses modulo logic", "[advanced][eval]") {
    auto root = std::make_shared<ConfigObjectNode>();
    root->set_child("complex_math", std::make_shared<ConfigValueNode>("${eval: (10 + 2) * (5 - 2)}")); // 12 * 3 = 36
    root->set_child("modulo", std::make_shared<ConfigValueNode>("${eval: 23 % 5}")); // 3
    root->set_child("logic_and", std::make_shared<ConfigValueNode>("${eval: true && false}")); // False
    root->set_child("logic_or", std::make_shared<ConfigValueNode>("${eval: false || true}")); // True
    root->set_child("unary_not", std::make_shared<ConfigValueNode>("${eval: !false}")); // True
    root->set_child("comparison_le", std::make_shared<ConfigValueNode>("${eval: 10 <= 10}")); // True

    InterpolationEngine engine(root);
    engine.resolve_all();

    CHECK(root->get_child("complex_math")->as_value()->to_string() == "36");
    CHECK(root->get_child("modulo")->as_value()->to_string() == "3");
    CHECK(root->get_child("logic_and")->as_value()->to_string() == "False");
    CHECK(root->get_child("logic_or")->as_value()->to_string() == "True");
    CHECK(root->get_child("unary_not")->as_value()->to_string() == "True");
    CHECK(root->get_child("comparison_le")->as_value()->to_string() == "True");
}

// =========================================================================
// 7. Empty Files Parsing
// =========================================================================

TEST_CASE("EmptyFiles across formats return empty object node", "[advanced][parsers]") {
    auto node1 = YamlFormatParser::parse("");
    CHECK(node1->children().empty());

    auto node2 = YamlFormatParser::parse("   \n  \t  ");
    CHECK(node2->children().empty());

    auto node3 = YamlFormatParser::parse("{}");
    CHECK(node3->children().empty());
}

// =========================================================================
// 8. ServiceConfig Binding and Acronym SnakeCase
// =========================================================================

TEST_CASE("ServiceConfig binding and snake case works", "[advanced][binding]") {
    AdvancedTempFixture fixture;

    fixture.write_file("config.yaml", R"(
service_name: PaymentGateway
max_retries: 5
ssl_port: 8443
)");

    auto cfg = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build<ServiceConfig>();

    CHECK(cfg.service_name == "PaymentGateway");
    CHECK(cfg.max_retries == 5);
    CHECK(cfg.ssl_port == 8443);
}

// =========================================================================
// 9. CliGroupSwitchWithoutSlash
// =========================================================================

TEST_CASE("CliGroupSwitchWithoutSlash does not overwrite group object with scalar", "[advanced][cli]") {
    AdvancedTempFixture fixture;

    fixture.write_file("config.yaml", R"(
defaults:
  - db: sqlite
app_name: SwitchApp
)");

    fixture.write_file("db/sqlite.yaml", R"(
driver: sqlite
port: 0
)");

    fixture.write_file("db/postgres.yaml", R"(
driver: postgres
port: 5432
)");

    auto cfg = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_arg("db=postgres")
        .build<AppConfig>();

    REQUIRE(cfg.db.has_value());
    CHECK(cfg.db->driver == "postgres");
    CHECK(cfg.db->port == 5432);
}

// =========================================================================
// 10. MultiLevelCascading across three directory levels
// =========================================================================

TEST_CASE("MultiLevelCascading cascades across three directory levels", "[advanced][cascading]") {
    AdvancedTempFixture fixture;

    fixture.write_file("config.yaml", R"(
defaults:
  - db: cloud/aws/postgres
)");

    fixture.write_file("db/postgres.yaml", R"(
driver: postgres
port: 5432
max_connections: 50
)");

    fixture.write_file("db/cloud/postgres.yaml", R"(
ssl: true
max_connections*=: 2
)");

    fixture.write_file("db/cloud/aws/postgres.yaml", R"(
host: rds.amazonaws.com
max_connections+=: 25
)");

    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build_node();

    REQUIRE(node != nullptr);
    auto db = node->get_child("db");
    REQUIRE(db != nullptr);
    REQUIRE(db->is_object());

    auto* db_obj = db->as_object();
    CHECK(db_obj->get_child("driver")->as_value()->as_string() == "postgres");
    CHECK(db_obj->get_child("ssl")->as_value()->as_bool() == true);
    CHECK(db_obj->get_child("host")->as_value()->as_string() == "rds.amazonaws.com");
    CHECK(db_obj->get_child("max_connections")->as_value()->as_integer() == 125LL); // (50 * 2) + 25
}

// =========================================================================
// 11. YamlQuotedScalars and Scientific Notation
// =========================================================================

TEST_CASE("YamlQuotedScalars preserves string types and parses scientific notation", "[advanced][yaml]") {
    auto node = YamlFormatParser::parse(R"(
quoted_number: "12345"
single_quoted_bool: 'true'
quoted_null: "null"
scientific_float: 1e5
unquoted_number: 12345
)");

    auto* qn = node->get_child("quoted_number")->as_value();
    CHECK(qn->is_string());
    CHECK(qn->as_string() == "12345");

    auto* qb = node->get_child("single_quoted_bool")->as_value();
    CHECK(qb->is_string());
    CHECK(qb->as_string() == "true");

    auto* qnull = node->get_child("quoted_null")->as_value();
    CHECK(qnull->is_string());
    CHECK(qnull->as_string() == "null");

    auto* sf = node->get_child("scientific_float")->as_value();
    CHECK(sf->is_double());
    CHECK(sf->as_double() == 100000.0);

    auto* un = node->get_child("unquoted_number")->as_value();
    CHECK(un->is_integer());
    CHECK(un->as_integer() == 12345LL);
}

// =========================================================================
// 12. SweepEngine Bracketed List Literal Isolation
// =========================================================================

TEST_CASE("SweepEngine does not split bracketed list literals on commas", "[advanced][sweep]") {
    std::vector<std::string> args = {
        "-m",
        "workers=2,4",
        "tags=[gpu,fast,cluster]"
    };

    auto [non_sweep, matrix, keys_order] = SweepEngine::parse_sweep_args(args);
    (void)keys_order;

    REQUIRE(matrix.find("workers") != matrix.end());
    CHECK_THAT(matrix["workers"], Catch::Matchers::Equals(std::vector<std::string>{"2", "4"}));

    CHECK(matrix.find("tags") == matrix.end());
    REQUIRE(non_sweep.size() == 1);
    CHECK(non_sweep[0] == "tags=[gpu,fast,cluster]");
}

// =========================================================================
// 13. Deep Multi-Hop and Diamond Interpolation
// =========================================================================

TEST_CASE("DeepMultiHop and diamond interpolation resolves without false cycle detection", "[advanced][interpolation]") {
    auto root = std::make_shared<ConfigObjectNode>();
    root->set_child("env_name", std::make_shared<ConfigValueNode>("prod"));
    root->set_child("region", std::make_shared<ConfigValueNode>("eu-west"));
    root->set_child("cluster", std::make_shared<ConfigValueNode>("${env_name}-${region}"));
    root->set_child("host_prefix", std::make_shared<ConfigValueNode>("${cluster}.internal"));
    root->set_child("primary_url", std::make_shared<ConfigValueNode>("https://${host_prefix}/v1"));
    root->set_child("backup_url", std::make_shared<ConfigValueNode>("https://${host_prefix}/v2"));
    root->set_child("summary", std::make_shared<ConfigValueNode>("${primary_url} | ${backup_url}"));

    InterpolationEngine engine(root);
    engine.resolve_all();

    CHECK(root->get_child("cluster")->as_value()->as_string() == "prod-eu-west");
    CHECK(root->get_child("host_prefix")->as_value()->as_string() == "prod-eu-west.internal");
    CHECK(root->get_child("primary_url")->as_value()->as_string() == "https://prod-eu-west.internal/v1");
    CHECK(root->get_child("backup_url")->as_value()->as_string() == "https://prod-eu-west.internal/v2");
    CHECK(root->get_child("summary")->as_value()->as_string() == "https://prod-eu-west.internal/v1 | https://prod-eu-west.internal/v2");
}

// =========================================================================
// 14. List Indexing in Interpolation and CLI List Mutation Operators
// =========================================================================

TEST_CASE("List indexing in interpolation and CLI list mutation operators work", "[advanced][list]") {
    AdvancedTempFixture fixture;

    fixture.write_file("config.yaml", R"(
servers:
  - alpha.local
  - beta.local
  - gamma.local
primary_server: "${servers[0]}"
secondary_server: "${servers.1}"
)");

    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .add_cli_args({"servers+=[delta.local]", "servers-=[beta.local]"})
        .build_node();

    REQUIRE(node != nullptr);
    auto* servers = node->get_child("servers")->as_list();
    REQUIRE(servers != nullptr);
    REQUIRE(servers->size() == 3);
    CHECK((*servers)[0]->to_string() == "alpha.local");
    CHECK((*servers)[1]->to_string() == "gamma.local");
    CHECK((*servers)[2]->to_string() == "delta.local");

    CHECK(node->get_child("primary_server")->as_value()->as_string() == "alpha.local");
    CHECK(node->get_child("secondary_server")->as_value()->as_string() == "gamma.local");
}

// =========================================================================
// 15. Error Diagnostics and Operator Exception
// =========================================================================

TEST_CASE("ErrorDiagnostics reports accurate operator exception info", "[advanced][diagnostics]") {
    auto base_node = std::make_shared<ConfigValueNode>("text_value");
    auto bad_override = std::make_shared<ConfigValueNode>(true, ConfigOperator::Toggle, SourceInfo{"override.yaml", 12, 4});

    try {
        OperatorEngine::apply(base_node, bad_override);
        FAIL("Expected InvalidOperatorException");
    } catch (const InvalidOperatorException& ex) {
        CHECK(ex.op() == "!=");
        CHECK(ex.source_location().file_path == "override.yaml");
        CHECK(ex.source_location().line == 12);
        CHECK(ex.source_location().column == 4);
    }
}

// =========================================================================
// 16. StructuredBinding Dynamic Target
// =========================================================================

TEST_CASE("StructuredBinding instantiates dynamic target struct", "[advanced][binding]") {
    AdvancedTempFixture fixture;

    fixture.write_file("config.yaml", R"(
cache:
  _target_: "MemoryCacheService"
  prefix: custom_cache
)");

    auto cfg = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build<DynamicTargetConfig>();

    REQUIRE(cfg.cache.has_value());
    CHECK(cfg.cache->get_name() == "Memory:custom_cache");
}

// =========================================================================
// 17. ExtendedScalarTypes and Acronym SnakeCase
// =========================================================================

TEST_CASE("ExtendedScalarTypes and acronym snake case bind successfully", "[advanced][binding]") {
    AdvancedTempFixture fixture;

    fixture.write_file("config.yaml", R"(
endpoint_uri: "https://api.omegaconf.net/v1"
created_at: "2026-10-31T12:00:00+00:00"
release_date: "2026-10-31"
maintenance_window: "02:30:00"
ssl_port: 8443
https_endpoint: "https://secure.local"
)");

    auto cfg = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build<ExtendedTypesConfig>();

    CHECK(cfg.endpoint_uri == "https://api.omegaconf.net/v1");
    CHECK(cfg.created_at == "2026-10-31T12:00:00+00:00");
    CHECK(cfg.release_date == "2026-10-31");
    CHECK(cfg.maintenance_window == "02:30:00");
    CHECK(cfg.ssl_port == 8443);
    CHECK(cfg.https_endpoint == "https://secure.local");
}

// =========================================================================
// 18. Nested Custom Resolver Interpolation and TOML Serialization
// =========================================================================

TEST_CASE("Nested custom resolver interpolation and TOML serialization work", "[advanced][toml]") {
    auto root = std::make_shared<ConfigObjectNode>();
    root->set_child("app_name", std::make_shared<ConfigValueNode>("My Awesome Service"));
    root->set_child("slug", std::make_shared<ConfigValueNode>("${slugify:${app_name}}"));
    root->set_child("port", std::make_shared<ConfigValueNode>(8080LL));

    InterpolationEngine engine(root, { std::make_shared<SlugifyResolver>() });
    engine.resolve_all();

    CHECK(root->get_child("slug")->as_value()->as_string() == "my-awesome-service");

    std::string toml = root->to_toml();
    CHECK(toml.find("slug = \"my-awesome-service\"") != std::string::npos);
    CHECK(toml.find("port = 8080") != std::string::npos);
}

// =========================================================================
// 19. Cross-Format Tree Composition (YAML, JSONC, TOML)
// =========================================================================

TEST_CASE("CrossFormatTreeComposition merges YAML, JSONC, and TOML and applies operators", "[advanced][formats]") {
    AdvancedTempFixture fixture;

    // Base file in TOML
    fixture.write_file("base.toml", R"(
app_name = "CrossFormatService"
workers = 4
)");

    // Entrypoint in YAML inheriting from TOML and composing JSONC + TOML groups
    fixture.write_file("config.yaml", R"(
_inherits_: base.toml
defaults:
  - db: postgres
  - cache: redis
  - _self_
workers*=: 3
)");

    // db group in JSONC
    fixture.write_file("db/postgres.jsonc", R"(
{
  // JSONC database config
  "driver": "postgres",
  "port": 5432,
  "tags": ["primary", "sql",],
}
)");

    // cache group in TOML
    fixture.write_file("cache/redis.toml", R"(
provider = "redis"
ttl_seconds = 120
)");

    auto node = OmegaConfBuilder::create()
        .set_root(fixture.temp_dir.string())
        .build_node();

    REQUIRE(node != nullptr);
    CHECK(node->get_child("app_name")->as_value()->as_string() == "CrossFormatService");
    CHECK(node->get_child("workers")->as_value()->as_integer() == 12LL); // 4 * 3

    auto db = node->get_child("db");
    REQUIRE(db != nullptr);
    REQUIRE(db->is_object());
    auto* db_obj = db->as_object();
    CHECK(db_obj->get_child("driver")->as_value()->as_string() == "postgres");
    CHECK(db_obj->get_child("port")->as_value()->as_integer() == 5432LL);
    REQUIRE(db_obj->get_child("tags")->is_list());
    CHECK(db_obj->get_child("tags")->as_list()->size() == 2);

    auto cache = node->get_child("cache");
    REQUIRE(cache != nullptr);
    REQUIRE(cache->is_object());
    auto* cache_obj = cache->as_object();
    CHECK(cache_obj->get_child("provider")->as_value()->as_string() == "redis");
    CHECK(cache_obj->get_child("ttl_seconds")->as_value()->as_integer() == 120LL);
}
