#include <catch2/catch_test_macros.hpp>
#include <omegaconf/omegaconf.hpp>

using namespace omegaconf;

TEST_CASE("OperatorParser should extract suffix operators", "[operators]") {
    auto [k1, op1] = OperatorParser::parse_key("timeout+=");
    CHECK(k1 == "timeout");
    CHECK(op1 == ConfigOperator::Add);

    auto [k2, op2] = OperatorParser::parse_key("items-=");
    CHECK(k2 == "items");
    CHECK(op2 == ConfigOperator::Subtract);

    auto [k3, op3] = OperatorParser::parse_key("scale*=");
    CHECK(k3 == "scale");
    CHECK(op3 == ConfigOperator::Multiply);

    auto [k4, op4] = OperatorParser::parse_key("ratio/=");
    CHECK(k4 == "ratio");
    CHECK(op4 == ConfigOperator::Divide);

    auto [k5, op5] = OperatorParser::parse_key("flag&=");
    CHECK(k5 == "flag");
    CHECK(op5 == ConfigOperator::BitwiseAnd);

    auto [k6, op6] = OperatorParser::parse_key("flag|=");
    CHECK(k6 == "flag");
    CHECK(op6 == ConfigOperator::BitwiseOr);

    auto [k7, op7] = OperatorParser::parse_key("debug!=");
    CHECK(k7 == "debug");
    CHECK(op7 == ConfigOperator::Toggle);

    auto [k8, op8] = OperatorParser::parse_key("normal=");
    CHECK(k8 == "normal");
    CHECK(op8 == ConfigOperator::Assign);

    auto [k9, op9] = OperatorParser::parse_key("plain_key");
    CHECK(k9 == "plain_key");
    CHECK(op9 == ConfigOperator::Assign);
}

TEST_CASE("OperatorParser should parse assignment CLI expressions", "[operators]") {
    auto res1 = OperatorParser::parse_assignment("app.timeout+=10");
    CHECK(res1.path == "app.timeout");
    CHECK(res1.op == ConfigOperator::Add);
    CHECK(res1.value == "10");

    auto res2 = OperatorParser::parse_assignment("db.host=localhost");
    CHECK(res2.path == "db.host");
    CHECK(res2.op == ConfigOperator::Assign);
    CHECK(res2.value == "localhost");

    auto res3 = OperatorParser::parse_assignment("debug!=");
    CHECK(res3.path == "debug");
    CHECK(res3.op == ConfigOperator::Toggle);
    CHECK(res3.value == "");

    CHECK_THROWS_AS(OperatorParser::parse_assignment(""), ConfigSyntaxException);
    CHECK_THROWS_AS(OperatorParser::parse_assignment("no_operator_here"), ConfigSyntaxException);
}

TEST_CASE("AddOperator should add numbers, append to lists, and concatenate strings", "[operators]") {
    // Numeric Add
    auto base_num = std::make_shared<ConfigValueNode>(10);
    auto override_num = std::make_shared<ConfigValueNode>(5, ConfigOperator::Add);
    auto res_num = OperatorEngine::apply(base_num, override_num);
    REQUIRE(res_num->is_value());
    CHECK(res_num->as_value()->as_integer() == 15);

    // String Concat
    auto base_str = std::make_shared<ConfigValueNode>("Hello, ");
    auto override_str = std::make_shared<ConfigValueNode>("World!", ConfigOperator::Add);
    auto res_str = OperatorEngine::apply(base_str, override_str);
    REQUIRE(res_str->is_value());
    CHECK(res_str->as_value()->as_string() == "Hello, World!");

    // List Append
    std::vector<std::shared_ptr<ConfigNode>> base_items = { std::make_shared<ConfigValueNode>("item1") };
    auto base_list = std::make_shared<ConfigListNode>(base_items);

    std::vector<std::shared_ptr<ConfigNode>> override_items = { std::make_shared<ConfigValueNode>("item2") };
    auto override_list = std::make_shared<ConfigListNode>(override_items, ConfigOperator::Add);

    auto res_list = OperatorEngine::apply(base_list, override_list);
    REQUIRE(res_list->is_list());
    auto* l = res_list->as_list();
    REQUIRE(l->size() == 2);
    CHECK((*l)[0]->to_string() == "item1");
    CHECK((*l)[1]->to_string() == "item2");
}

TEST_CASE("SubtractOperator should subtract numbers and remove list items", "[operators]") {
    // Numeric Subtract
    auto base_num = std::make_shared<ConfigValueNode>(20);
    auto override_num = std::make_shared<ConfigValueNode>(7, ConfigOperator::Subtract);
    auto res_num = OperatorEngine::apply(base_num, override_num);
    REQUIRE(res_num->is_value());
    CHECK(res_num->as_value()->as_integer() == 13);

    // List item removal
    std::vector<std::shared_ptr<ConfigNode>> base_items = {
        std::make_shared<ConfigValueNode>("a"),
        std::make_shared<ConfigValueNode>("b"),
        std::make_shared<ConfigValueNode>("c")
    };
    auto base_list = std::make_shared<ConfigListNode>(base_items);

    std::vector<std::shared_ptr<ConfigNode>> override_items = {
        std::make_shared<ConfigValueNode>("b")
    };
    auto override_list = std::make_shared<ConfigListNode>(override_items, ConfigOperator::Subtract);

    auto res_list = OperatorEngine::apply(base_list, override_list);
    REQUIRE(res_list->is_list());
    auto* l = res_list->as_list();
    REQUIRE(l->size() == 2);
    CHECK((*l)[0]->to_string() == "a");
    CHECK((*l)[1]->to_string() == "c");
}

TEST_CASE("Multiply and Divide operators should work on numbers and strings", "[operators]") {
    // Multiply
    auto base_num = std::make_shared<ConfigValueNode>(25);
    auto mult_num = std::make_shared<ConfigValueNode>(4, ConfigOperator::Multiply);
    auto res_mult = OperatorEngine::apply(base_num, mult_num);
    REQUIRE(res_mult->is_value());
    CHECK(res_mult->as_value()->as_integer() == 100);

    // Divide
    auto div_num = std::make_shared<ConfigValueNode>(5, ConfigOperator::Divide);
    auto res_div = OperatorEngine::apply(res_mult, div_num);
    REQUIRE(res_div->is_value());
    CHECK(res_div->as_value()->as_integer() == 20);

    // String repetition
    auto base_str = std::make_shared<ConfigValueNode>("ha");
    auto rep_num = std::make_shared<ConfigValueNode>(3, ConfigOperator::Multiply);
    auto res_rep = OperatorEngine::apply(base_str, rep_num);
    REQUIRE(res_rep->is_value());
    CHECK(res_rep->as_value()->as_string() == "hahaha");
}

TEST_CASE("Boolean operators should apply logic correctly", "[operators]") {
    auto bool_true = std::make_shared<ConfigValueNode>(true);
    auto bool_false = std::make_shared<ConfigValueNode>(false);

    // AND
    auto and_override = std::make_shared<ConfigValueNode>(false, ConfigOperator::BitwiseAnd);
    auto res_and = OperatorEngine::apply(bool_true, and_override);
    REQUIRE(res_and->is_value());
    CHECK(res_and->as_value()->as_bool() == false);

    // OR
    auto or_override = std::make_shared<ConfigValueNode>(true, ConfigOperator::BitwiseOr);
    auto res_or = OperatorEngine::apply(bool_false, or_override);
    REQUIRE(res_or->is_value());
    CHECK(res_or->as_value()->as_bool() == true);

    // Toggle (!=)
    auto toggle = std::make_shared<ConfigValueNode>(true, ConfigOperator::Toggle);
    auto res_toggle = OperatorEngine::apply(bool_true, toggle);
    REQUIRE(res_toggle->is_value());
    CHECK(res_toggle->as_value()->as_bool() == false);
}

TEST_CASE("Incompatible operator should throw InvalidOperatorException", "[operators]") {
    auto base_val = std::make_shared<ConfigValueNode>("not a boolean");
    auto toggle = std::make_shared<ConfigValueNode>(true, ConfigOperator::Toggle);

    CHECK_THROWS_AS(OperatorEngine::apply(base_val, toggle), InvalidOperatorException);
}
