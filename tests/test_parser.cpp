#include <catch2/catch_test_macros.hpp>
#include <omegaconf/omegaconf.hpp>
#include <omegaconf/parsers/yaml_format_parser.hpp>

using namespace omegaconf;

TEST_CASE("YamlParser should parse nested mappings and operators", "[parser]") {
    std::string yaml = R"(
app_name: MyAwesomeApp
timeout+=: 10
features:
  - auth
  - logging
db:
  host: localhost
  port: 5432
  password: ???
)";

    auto node = YamlFormatParser::parse(yaml, "app.yaml");
    REQUIRE(node != nullptr);

    auto app_name_node = node->get_child("app_name");
    REQUIRE(app_name_node != nullptr);
    REQUIRE(app_name_node->is_value());
    CHECK(app_name_node->as_value()->as_string() == "MyAwesomeApp");

    auto timeout_node = node->get_child("timeout");
    REQUIRE(timeout_node != nullptr);
    REQUIRE(timeout_node->is_value());
    CHECK(timeout_node->as_value()->as_integer() == 10);
    CHECK(timeout_node->op() == ConfigOperator::Add);

    auto features_node = node->get_child("features");
    REQUIRE(features_node != nullptr);
    REQUIRE(features_node->is_list());
    CHECK(features_node->as_list()->size() == 2);
    CHECK((*features_node->as_list())[0]->to_string() == "auth");
    CHECK((*features_node->as_list())[1]->to_string() == "logging");

    auto db_node = node->get_child("db");
    REQUIRE(db_node != nullptr);
    REQUIRE(db_node->is_object());
    auto* db_obj = db_node->as_object();

    auto port_node = db_obj->get_child("port");
    REQUIRE(port_node != nullptr);
    REQUIRE(port_node->is_value());
    CHECK(port_node->as_value()->as_integer() == 5432);

    auto pwd_node = db_obj->get_child("password");
    REQUIRE(pwd_node != nullptr);
    REQUIRE(pwd_node->is_value());
    CHECK(pwd_node->as_value()->is_missing_marker() == true);
}

TEST_CASE("JsonParser should support jsonc and trailing commas", "[parser]") {
    std::string jsonc = R"({
  // Application configuration
  "service_name": "WorkerService",
  "threads*=": 2,
  "tags": ["worker", "high-priority",],
})";

    auto node = YamlFormatParser::parse(jsonc, "config.jsonc");
    REQUIRE(node != nullptr);

    auto svc_node = node->get_child("service_name");
    REQUIRE(svc_node != nullptr);
    REQUIRE(svc_node->is_value());
    CHECK(svc_node->as_value()->as_string() == "WorkerService");

    auto threads_node = node->get_child("threads");
    REQUIRE(threads_node != nullptr);
    REQUIRE(threads_node->is_value());
    CHECK(threads_node->as_value()->as_integer() == 2);
    CHECK(threads_node->op() == ConfigOperator::Multiply);

    auto tags_node = node->get_child("tags");
    REQUIRE(tags_node != nullptr);
    REQUIRE(tags_node->is_list());
    CHECK(tags_node->as_list()->size() == 2);
}
