#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "omegaconf/omegaconf.hpp"
#include "omegaconf/diff/config_differ.hpp"

#include <climits>

using namespace omegaconf;

// ------------------------------------------------------------------------
// Regression tests for defects fixed during Hydra-parity audit
// ------------------------------------------------------------------------

TEST_CASE("Regression YamlMalformedInput throws ConfigSyntaxException", "[regression]") {
    std::string malformed = "a:\n  - 1\n  - [2, 3\nb: 1";
    CHECK_THROWS_AS(YamlFormatParser::parse(malformed), ConfigSyntaxException);
}

TEST_CASE("Regression YamlBooleanSpellings are parsed as boolean", "[regression]") {
    auto obj1 = YamlFormatParser::parse("flag: yes");
    CHECK(obj1->get_child("flag")->as_value()->as_bool() == true);

    auto obj2 = YamlFormatParser::parse("flag: no");
    CHECK(obj2->get_child("flag")->as_value()->as_bool() == false);

    auto obj3 = YamlFormatParser::parse("flag: on");
    CHECK(obj3->get_child("flag")->as_value()->as_bool() == true);

    auto obj4 = YamlFormatParser::parse("flag: off");
    CHECK(obj4->get_child("flag")->as_value()->as_bool() == false);

    auto obj5 = YamlFormatParser::parse("flag: YES");
    CHECK(obj5->get_child("flag")->as_value()->as_bool() == true);

    auto obj6 = YamlFormatParser::parse("flag: Off");
    CHECK(obj6->get_child("flag")->as_value()->as_bool() == false);
}

TEST_CASE("Regression EvalModulo preserves floating point operands", "[regression]") {
    auto root = std::make_shared<ConfigObjectNode>();
    root->set_child("result", std::make_shared<ConfigValueNode>("${eval: 5.5 % 2.0}"));

    InterpolationEngine engine(root);
    engine.resolve_all();

    CHECK(root->get_child("result")->as_value()->as_string() == "1.5");
}

TEST_CASE("Regression EvalDivisionByZero throws ConfigSyntaxException", "[regression]") {
    {
        auto root = std::make_shared<ConfigObjectNode>();
        root->set_child("result", std::make_shared<ConfigValueNode>("${eval: 5 / 0}"));
        InterpolationEngine engine(root);
        CHECK_THROWS_AS(engine.resolve_all(), ConfigSyntaxException);
    }
    {
        auto root = std::make_shared<ConfigObjectNode>();
        root->set_child("result", std::make_shared<ConfigValueNode>("${eval: 5 % 0}"));
        InterpolationEngine engine(root);
        CHECK_THROWS_AS(engine.resolve_all(), ConfigSyntaxException);
    }
}

// ------------------------------------------------------------------------
// Coverage gaps: ConfigDiffer branches
// ------------------------------------------------------------------------

TEST_CASE("ConfigDiffer sibling and identical trees report no changes", "[diff]") {
    auto a = std::make_shared<ConfigObjectNode>();
    a->set_child("host", std::make_shared<ConfigValueNode>("localhost"));
    a->set_child("port", std::make_shared<ConfigValueNode>(5432LL));

    auto b = std::make_shared<ConfigObjectNode>();
    b->set_child("host", std::make_shared<ConfigValueNode>("localhost"));
    b->set_child("port", std::make_shared<ConfigValueNode>(5432LL));

    auto diff = ConfigDiffer::compare(a, b);
    CHECK_FALSE(diff.has_changes());
    CHECK(diff.to_string() == "No configuration differences.");
}

TEST_CASE("ConfigDiffer nested list modification is reported as modified", "[diff]") {
    auto base_list = std::make_shared<ConfigListNode>();
    base_list->add_item(std::make_shared<ConfigValueNode>(1LL));
    base_list->add_item(std::make_shared<ConfigValueNode>(2LL));
    base_list->add_item(std::make_shared<ConfigValueNode>(3LL));

    auto base_obj = std::make_shared<ConfigObjectNode>();
    base_obj->set_child("items", base_list);

    auto target_list = std::make_shared<ConfigListNode>();
    target_list->add_item(std::make_shared<ConfigValueNode>(1LL));
    target_list->add_item(std::make_shared<ConfigValueNode>(2LL));
    target_list->add_item(std::make_shared<ConfigValueNode>(4LL));

    auto target_obj = std::make_shared<ConfigObjectNode>();
    target_obj->set_child("items", target_list);

    auto diff = ConfigDiffer::compare(base_obj, target_obj);
    CHECK(diff.has_changes());
    REQUIRE(diff.entries().size() == 1);
    CHECK(diff.entries()[0].change_type == ConfigDiffChangeType::Modified);
    CHECK(diff.to_string().find("items:") != std::string::npos);
    CHECK(diff.to_string().find("->") != std::string::npos);
}

TEST_CASE("ConfigDiffer scalar to scalar type change is reported as modified", "[diff]") {
    auto base_obj = std::make_shared<ConfigObjectNode>();
    base_obj->set_child("value", std::make_shared<ConfigValueNode>(123LL));

    auto target_obj = std::make_shared<ConfigObjectNode>();
    target_obj->set_child("value", std::make_shared<ConfigValueNode>("abc"));

    auto diff = ConfigDiffer::compare(base_obj, target_obj);
    REQUIRE(diff.entries().size() == 1);
    CHECK(diff.entries()[0].change_type == ConfigDiffChangeType::Modified);
    CHECK(diff.to_string().find("value: 123 -> \"abc\"") != std::string::npos);
}

TEST_CASE("ConfigDiffer missing marker is rendered as question marks", "[diff]") {
    auto base_obj = std::make_shared<ConfigObjectNode>();
    base_obj->set_child("token", std::make_shared<ConfigValueNode>(std::string(ConfigValueNode::MissingMarker)));

    auto target_obj = std::make_shared<ConfigObjectNode>();
    target_obj->set_child("token", std::make_shared<ConfigValueNode>("settled"));

    auto diff = ConfigDiffer::compare(base_obj, target_obj);
    CHECK(diff.to_string().find("???") != std::string::npos);
}

// ------------------------------------------------------------------------
// Coverage gap: SweepEngine range/linspace validation throws
// ------------------------------------------------------------------------

TEST_CASE("SweepValidation throws on zero step or count", "[sweep]") {
    CHECK_THROWS_AS(SweepEngine::parse_sweep_args({"-m", "workers=range(1, 10, 0)"}), ConfigSyntaxException);
    CHECK_THROWS_AS(SweepEngine::parse_sweep_args({"-m", "lr=linspace(0, 1, 0)"}), ConfigSyntaxException);
}

TEST_CASE("SweepValidation throws on non-numeric generator", "[sweep]") {
    CHECK_THROWS(SweepEngine::parse_sweep_args({"-m", "workers=range(abc)"}));
}

// ------------------------------------------------------------------------
// Coverage gap: ${env:VAR} with no default and unset var throws
// ------------------------------------------------------------------------

TEST_CASE("Interpolation unset env without default throws ConfigInterpolationException", "[interpolation]") {
    auto root = std::make_shared<ConfigObjectNode>();
    root->set_child("value", std::make_shared<ConfigValueNode>("${env:OMEGACONF_AUDIT_UNSET_VAR_XYZ123}"));

    InterpolationEngine engine(root);
    CHECK_THROWS_AS(engine.resolve_all(), ConfigInterpolationException);
}
