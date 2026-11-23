#include <catch2/catch_test_macros.hpp>
#include <omegaconf/omegaconf.hpp>
#include <omegaconf/interpolation/interpolation_engine.hpp>

#include <cstdlib>

using namespace omegaconf;

TEST_CASE("Interpolation should resolve paths and preserve types", "[interpolation]") {
    auto root = std::make_shared<ConfigObjectNode>();
    auto server_obj = std::make_shared<ConfigObjectNode>();
    server_obj->set_child("host", std::make_shared<ConfigValueNode>("10.0.0.1"));
    server_obj->set_child("port", std::make_shared<ConfigValueNode>(8080));
    root->set_child("server", server_obj);

    root->set_child("target_port", std::make_shared<ConfigValueNode>("${server.port}"));
    root->set_child("url", std::make_shared<ConfigValueNode>("http://${server.host}:${server.port}/api"));

    InterpolationEngine engine(root);
    engine.resolve_all();

    // Native type preservation
    auto port_node = root->get_child("target_port");
    REQUIRE(port_node != nullptr);
    REQUIRE(port_node->is_value());
    CHECK(port_node->as_value()->is_integer());
    CHECK(port_node->as_value()->as_integer() == 8080);

    // String interpolation
    auto url_node = root->get_child("url");
    REQUIRE(url_node != nullptr);
    REQUIRE(url_node->is_value());
    CHECK(url_node->as_value()->as_string() == "http://10.0.0.1:8080/api");
}

TEST_CASE("Interpolation should support relative and sibling dot syntax", "[interpolation]") {
    auto root = std::make_shared<ConfigObjectNode>();
    auto db = std::make_shared<ConfigObjectNode>();
    db->set_child("host", std::make_shared<ConfigValueNode>("prod-db"));
    db->set_child("port", std::make_shared<ConfigValueNode>(5432));

    auto staging = std::make_shared<ConfigObjectNode>();
    staging->set_child("port", std::make_shared<ConfigValueNode>(5433));
    staging->set_child("connection_string", std::make_shared<ConfigValueNode>("Server=${..host};Port=${.port}"));

    db->set_child("staging", staging);
    root->set_child("db", db);

    InterpolationEngine engine(root);
    engine.resolve_all();

    auto conn_node = staging->get_child("connection_string");
    REQUIRE(conn_node != nullptr);
    REQUIRE(conn_node->is_value());
    CHECK(conn_node->as_value()->as_string() == "Server=prod-db;Port=5433");
}

TEST_CASE("Interpolation should support literal escaping", "[interpolation]") {
    auto root = std::make_shared<ConfigObjectNode>();
    root->set_child("server_port", std::make_shared<ConfigValueNode>(8080));
    root->set_child("bash_command", std::make_shared<ConfigValueNode>("echo \\${HOME} on port ${server_port}"));
    root->set_child("raw_template", std::make_shared<ConfigValueNode>("\\${server_port}"));

    InterpolationEngine engine(root);
    engine.resolve_all();

    auto bash_node = root->get_child("bash_command");
    REQUIRE(bash_node != nullptr);
    REQUIRE(bash_node->is_value());
    CHECK(bash_node->as_value()->as_string() == "echo ${HOME} on port 8080");

    auto raw_node = root->get_child("raw_template");
    REQUIRE(raw_node != nullptr);
    REQUIRE(raw_node->is_value());
    CHECK(raw_node->as_value()->as_string() == "${server_port}");
}

TEST_CASE("Interpolation should evaluate math and expressions", "[interpolation]") {
    auto root = std::make_shared<ConfigObjectNode>();
    root->set_child("batch_size", std::make_shared<ConfigValueNode>(32));
    root->set_child("num_gpus", std::make_shared<ConfigValueNode>(4));
    root->set_child("total_batch", std::make_shared<ConfigValueNode>("${eval: ${batch_size} * ${num_gpus}}"));
    root->set_child("order_of_ops", std::make_shared<ConfigValueNode>("${eval: 10 + 5 * 2}"));
    root->set_child("division", std::make_shared<ConfigValueNode>("${eval: 100 / 4}"));
    root->set_child("comparison", std::make_shared<ConfigValueNode>("${eval: 5 > 2}"));

    InterpolationEngine engine(root);
    engine.resolve_all();

    CHECK(root->get_child("total_batch")->as_value()->as_string() == "128");
    CHECK(root->get_child("order_of_ops")->as_value()->as_string() == "20");
    CHECK(root->get_child("division")->as_value()->as_string() == "25");
    CHECK(root->get_child("comparison")->as_value()->as_string() == "True");
}

TEST_CASE("Interpolation should preserve raw template formula", "[interpolation]") {
    auto root = std::make_shared<ConfigObjectNode>();
    root->set_child("port", std::make_shared<ConfigValueNode>(5432));
    auto template_node = std::make_shared<ConfigValueNode>("http://localhost:${port}/");
    root->set_child("url", template_node);

    InterpolationEngine engine(root);
    engine.resolve_all();

    CHECK(template_node->as_value()->as_string() == "http://localhost:5432/");
    CHECK(template_node->raw_template() == "http://localhost:${port}/");
}

TEST_CASE("Interpolation should use default fallback when path missing", "[interpolation]") {
    auto root = std::make_shared<ConfigObjectNode>();
    root->set_child("timeout", std::make_shared<ConfigValueNode>("${missing.timeout:30}"));

    InterpolationEngine engine(root);
    engine.resolve_all();

    CHECK(root->get_child("timeout")->as_value()->as_string() == "30");
}

TEST_CASE("Interpolation should detect circular references", "[interpolation]") {
    auto root = std::make_shared<ConfigObjectNode>();
    root->set_child("a", std::make_shared<ConfigValueNode>("${b}"));
    root->set_child("b", std::make_shared<ConfigValueNode>("${a}"));

    InterpolationEngine engine(root);
    CHECK_THROWS_AS(engine.resolve_all(), ConfigInterpolationException);
}

TEST_CASE("Interpolation should resolve environment variables and custom resolvers", "[interpolation]") {
#if defined(_MSC_VER)
    _putenv("OMEGA_TEST_VAR=Secret123");
#else
    setenv("OMEGA_TEST_VAR", "Secret123", 1);
#endif

    auto root = std::make_shared<ConfigObjectNode>();
    root->set_child("api_key", std::make_shared<ConfigValueNode>("${env:OMEGA_TEST_VAR}"));
    root->set_child("fallback_key", std::make_shared<ConfigValueNode>("${env:NON_EXISTENT_VAR, default_secret}"));
    root->set_child("upper_text", std::make_shared<ConfigValueNode>("${upper:hello}"));

    auto upper_resolver = std::make_shared<CustomResolver>("upper", [](std::string_view s) {
        std::string res(s);
        for (char& c : res) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return res;
    });

    InterpolationEngine engine(root, { upper_resolver });
    engine.resolve_all();

    CHECK(root->get_child("api_key")->as_value()->as_string() == "Secret123");
    CHECK(root->get_child("fallback_key")->as_value()->as_string() == "default_secret");
    CHECK(root->get_child("upper_text")->as_value()->as_string() == "HELLO");

#if defined(_MSC_VER)
    _putenv("OMEGA_TEST_VAR=");
#else
    unsetenv("OMEGA_TEST_VAR");
#endif
}

TEST_CASE("Interpolation referencing missing marker should throw", "[interpolation]") {
    auto root = std::make_shared<ConfigObjectNode>();
    root->set_child("token", std::make_shared<ConfigValueNode>(std::string(ConfigValueNode::MissingMarker)));
    root->set_child("auth_header", std::make_shared<ConfigValueNode>("Bearer ${token}"));

    InterpolationEngine engine(root);
    CHECK_THROWS_AS(engine.resolve_all(), MissingValueMarkerException);
}
