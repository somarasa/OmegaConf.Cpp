#include <catch2/catch_test_macros.hpp>
#include <omegaconf/omegaconf.hpp>

using namespace omegaconf;

TEST_CASE("ConfigValueNode missing marker and escaping", "[ast]") {
    ConfigValueNode node_missing("???");
    CHECK(node_missing.is_missing_marker());
    CHECK_FALSE(node_missing.is_escaped_literal());
    CHECK(node_missing.to_string() == "???");

    ConfigValueNode node_escaped("\\???");
    CHECK_FALSE(node_escaped.is_missing_marker());
    CHECK(node_escaped.is_escaped_literal());
    CHECK(node_escaped.as_string() == "???");

    ConfigValueNode node_double_escaped("\\\\???");
    CHECK_FALSE(node_double_escaped.is_missing_marker());
    CHECK(node_double_escaped.is_escaped_literal());
    CHECK(node_double_escaped.as_string() == "\\???");
}

TEST_CASE("ConfigValueNode scalar conversions", "[ast]") {
    ConfigValueNode node_int(42);
    CHECK(node_int.is_integer());
    CHECK(node_int.as_integer() == 42);
    CHECK(node_int.as_double() == 42.0);
    CHECK(node_int.as_bool() == true);
    CHECK(node_int.to_string() == "42");

    ConfigValueNode node_double(3.14);
    CHECK(node_double.is_double());
    CHECK(node_double.as_double() == 3.14);

    ConfigValueNode node_bool(true);
    CHECK(node_bool.is_bool());
    CHECK(node_bool.as_bool() == true);
    CHECK(node_bool.to_string() == "true");

    ConfigValueNode node_null(nullptr);
    CHECK(node_null.is_null());
    CHECK(node_null.to_string() == "null");
}

TEST_CASE("ConfigListNode operations and clone", "[ast]") {
    auto list = std::make_shared<ConfigListNode>();
    list->add_item(std::make_shared<ConfigValueNode>("alpha"));
    list->add_item(std::make_shared<ConfigValueNode>("beta"));

    CHECK(list->size() == 2);
    CHECK((*list)[0]->to_string() == "alpha");
    CHECK((*list)[1]->to_string() == "beta");
    CHECK(list->to_string() == "[alpha, beta]");

    auto cloned = list->clone();
    REQUIRE(cloned->is_list());
    CHECK(cloned->as_list()->size() == 2);

    list->freeze();
    CHECK(list->is_read_only());
    CHECK((*list)[0]->is_read_only());
    CHECK_THROWS_AS(list->add_item(std::make_shared<ConfigValueNode>("gamma")), ConfigReadOnlyException);
}

TEST_CASE("ConfigObjectNode case-insensitivity and select_node", "[ast]") {
    auto obj = std::make_shared<ConfigObjectNode>();
    obj->set_child("Host", std::make_shared<ConfigValueNode>("localhost"));
    obj->set_child("Port", std::make_shared<ConfigValueNode>(5432));

    // Case-insensitive lookup
    CHECK(obj->contains_key("host"));
    CHECK(obj->contains_key("HOST"));
    CHECK(obj->get_child("host")->as_value()->as_string() == "localhost");
    CHECK(obj->get_child("PORT")->as_value()->as_integer() == 5432);

    // Nested object
    auto db = std::make_shared<ConfigObjectNode>();
    db->set_child("primary", obj);

    // List in object
    auto servers = std::make_shared<ConfigListNode>();
    servers->add_item(std::make_shared<ConfigValueNode>("srv1"));
    servers->add_item(std::make_shared<ConfigValueNode>("srv2"));
    db->set_child("servers", servers);

    // Path selection: dot notation
    auto selected_host = db->select_node("primary.host");
    REQUIRE(selected_host != nullptr);
    CHECK(selected_host->as_value()->as_string() == "localhost");

    // Path selection: bracket indexing
    auto selected_srv0 = db->select_node("servers[0]");
    REQUIRE(selected_srv0 != nullptr);
    CHECK(selected_srv0->as_value()->as_string() == "srv1");

    // Path selection: negative indexing
    auto selected_last = db->select_node("servers[-1]");
    REQUIRE(selected_last != nullptr);
    CHECK(selected_last->as_value()->as_string() == "srv2");

    // Path selection: dot indexing for lists
    auto selected_srv1_dot = db->select_node("servers.1");
    REQUIRE(selected_srv1_dot != nullptr);
    CHECK(selected_srv1_dot->as_value()->as_string() == "srv2");
}

TEST_CASE("ConfigObjectNode Struct Mode enforcement", "[ast]") {
    auto obj = std::make_shared<ConfigObjectNode>();
    obj->set_child("name", std::make_shared<ConfigValueNode>("app"));
    obj->set_struct(true);

    CHECK(obj->is_struct());

    // Updating existing key is allowed
    obj->set_child("name", std::make_shared<ConfigValueNode>("new_app"));
    CHECK(obj->get_child("name")->as_value()->as_string() == "new_app");

    // Adding new key throws ConfigStructException
    CHECK_THROWS_AS(obj->set_child("new_key", std::make_shared<ConfigValueNode>(123)), ConfigStructException);

    // Adding new key with allow_new_key_in_struct is allowed (e.g. +key override)
    CHECK_NOTHROW(obj->set_child("new_key", std::make_shared<ConfigValueNode>(123), true));
    CHECK(obj->get_child("new_key")->as_value()->as_integer() == 123);
}
