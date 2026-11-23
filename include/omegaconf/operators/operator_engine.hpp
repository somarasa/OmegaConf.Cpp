#pragma once

#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include "omegaconf/ast/config_list_node.hpp"
#include "omegaconf/ast/config_object_node.hpp"
#include "omegaconf/common/exceptions.hpp"
#include <memory>
#include <string>
#include <limits>
#include <cmath>

namespace omegaconf {

/**
 * @brief Executes in-config and CLI mutation operators (=, +=, -=, *=, /=, &=, |=, !=)
 * when merging an override ConfigNode onto an inherited base ConfigNode.
 * All operations return newly cloned nodes and never mutate the input AST nodes in-place.
 */
class OperatorEngine {
public:
    /**
     * @brief Merges an override_node onto a base_node using the operator declared on override_node.
     */
    static std::shared_ptr<ConfigNode> apply(const std::shared_ptr<ConfigNode>& base_node,
                                            const std::shared_ptr<ConfigNode>& override_node) {
        if (!override_node) {
            return base_node ? base_node->clone() : nullptr;
        }

        if (!base_node) {
            return override_node->clone();
        }

        switch (override_node->op()) {
            case ConfigOperator::Assign:
                return apply_assign(base_node, override_node);
            case ConfigOperator::Add:
                return apply_add(base_node, override_node);
            case ConfigOperator::Subtract:
                return apply_subtract(base_node, override_node);
            case ConfigOperator::Multiply:
                return apply_multiply(base_node, override_node);
            case ConfigOperator::Divide:
                return apply_divide(base_node, override_node);
            case ConfigOperator::BitwiseAnd:
                return apply_bitwise_and(base_node, override_node);
            case ConfigOperator::BitwiseOr:
                return apply_bitwise_or(base_node, override_node);
            case ConfigOperator::Toggle:
                return apply_toggle(base_node, override_node);
        }

        throw InvalidOperatorException(std::string(to_string(override_node->op())),
                                       std::string(to_string(base_node->node_type())),
                                       "Unknown operator",
                                       override_node->source());
    }

private:
    enum class NumericOp { Add, Subtract, Multiply, Divide };

    static std::shared_ptr<ConfigNode> apply_assign(const std::shared_ptr<ConfigNode>& base_node,
                                                   const std::shared_ptr<ConfigNode>& override_node) {
        if (base_node->is_object() && override_node->is_object()) {
            auto* base_obj = base_node->as_object();
            auto* override_obj = override_node->as_object();

            auto result = std::static_pointer_cast<ConfigObjectNode>(base_obj->clone());
            result->set_source(override_node->source());

            for (const auto& key : override_obj->keys()) {
                auto child = override_obj->get_child(key);
                if (!child) continue;

                auto existing = result->get_child(key);
                if (existing) {
                    result->set_child(key, apply(existing, child), true);
                } else {
                    result->set_child(key, child->clone(), true);
                }
            }
            return result;
        }

        // If baseNode is a composed ConfigObjectNode and overrideNode is a null/missing uninitialized schema placeholder
        if (base_node->is_object() && override_node->is_value()) {
            auto* ov_val = override_node->as_value();
            if (ov_val->is_null() || ov_val->is_missing_marker()) {
                return base_node->clone();
            }
        }

        return override_node->clone();
    }

    static std::shared_ptr<ConfigNode> apply_add(const std::shared_ptr<ConfigNode>& base_node,
                                                const std::shared_ptr<ConfigNode>& override_node) {
        // Case 1: List append
        if (base_node->is_list()) {
            auto result = std::static_pointer_cast<ConfigListNode>(base_node->clone());
            result->set_source(override_node->source());

            if (override_node->is_list()) {
                auto* override_list = override_node->as_list();
                for (size_t i = 0; i < override_list->size(); ++i) {
                    result->add_item((*override_list)[i] ? (*override_list)[i]->clone() : nullptr);
                }
            } else {
                result->add_item(override_node->clone());
            }
            return result;
        }

        // Case 2: Object merge
        if (base_node->is_object() && override_node->is_object()) {
            return apply_assign(base_node, override_node);
        }

        // Case 3: Scalar numeric addition or string concatenation
        if (base_node->is_value() && override_node->is_value()) {
            auto* base_val = base_node->as_value();
            auto* override_val = override_node->as_value();

            ScalarValue num_res{};
            if (try_perform_numeric_op(base_val, override_val, NumericOp::Add, num_res)) {
                return std::make_shared<ConfigValueNode>(num_res, ConfigOperator::Assign, override_node->source());
            }

            // Fallback to string concatenation
            std::string str_a = base_val->as_string();
            std::string str_b = override_val->as_string();
            return std::make_shared<ConfigValueNode>(str_a + str_b, ConfigOperator::Assign, override_node->source());
        }

        throw InvalidOperatorException("+=",
                                       std::string(to_string(base_node->node_type())),
                                       "Cannot add " + std::string(to_string(override_node->node_type())) + " to " + std::string(to_string(base_node->node_type())),
                                       override_node->source());
    }

    static std::shared_ptr<ConfigNode> apply_subtract(const std::shared_ptr<ConfigNode>& base_node,
                                                     const std::shared_ptr<ConfigNode>& override_node) {
        // Case 1: List item removal by string match
        if (base_node->is_list()) {
            auto result = std::static_pointer_cast<ConfigListNode>(base_node->clone());
            result->set_source(override_node->source());

            std::vector<std::string> to_remove_strs;
            if (override_node->is_list()) {
                auto* override_list = override_node->as_list();
                for (size_t i = 0; i < override_list->size(); ++i) {
                    if ((*override_list)[i]) {
                        to_remove_strs.push_back((*override_list)[i]->to_string());
                    }
                }
            } else {
                to_remove_strs.push_back(override_node->to_string());
            }

            auto& items = result->items();
            items.erase(std::remove_if(items.begin(), items.end(),
                [&to_remove_strs](const std::shared_ptr<ConfigNode>& item) {
                    if (!item) return false;
                    std::string s = item->to_string();
                    return std::find(to_remove_strs.begin(), to_remove_strs.end(), s) != to_remove_strs.end();
                }), items.end());

            return result;
        }

        // Case 2: Numeric subtraction
        if (base_node->is_value() && override_node->is_value()) {
            auto* base_val = base_node->as_value();
            auto* override_val = override_node->as_value();

            ScalarValue num_res{};
            if (try_perform_numeric_op(base_val, override_val, NumericOp::Subtract, num_res)) {
                return std::make_shared<ConfigValueNode>(num_res, ConfigOperator::Assign, override_node->source());
            }
        }

        throw InvalidOperatorException("-=",
                                       std::string(to_string(base_node->node_type())),
                                       "Cannot subtract " + std::string(to_string(override_node->node_type())) + " from " + std::string(to_string(base_node->node_type())),
                                       override_node->source());
    }

    static std::shared_ptr<ConfigNode> apply_multiply(const std::shared_ptr<ConfigNode>& base_node,
                                                     const std::shared_ptr<ConfigNode>& override_node) {
        if (base_node->is_value() && override_node->is_value()) {
            auto* base_val = base_node->as_value();
            auto* override_val = override_node->as_value();

            // Numeric multiplication
            ScalarValue num_res{};
            if (try_perform_numeric_op(base_val, override_val, NumericOp::Multiply, num_res)) {
                return std::make_shared<ConfigValueNode>(num_res, ConfigOperator::Assign, override_node->source());
            }

            // String repetition: "ha" *= 3 -> "hahaha"
            if (base_val->is_string() && override_val->is_integer()) {
                const auto& s = base_val->as_string();
                int64_t count = override_val->as_integer();
                std::string repeated;
                if (count > 0) {
                    repeated.reserve(s.size() * static_cast<size_t>(count));
                    for (int64_t i = 0; i < count; ++i) {
                        repeated.append(s);
                    }
                }
                return std::make_shared<ConfigValueNode>(repeated, ConfigOperator::Assign, override_node->source());
            }
        }

        throw InvalidOperatorException("*=",
                                       std::string(to_string(base_node->node_type())),
                                       "Cannot multiply " + std::string(to_string(base_node->node_type())) + " by " + std::string(to_string(override_node->node_type())),
                                       override_node->source());
    }

    static std::shared_ptr<ConfigNode> apply_divide(const std::shared_ptr<ConfigNode>& base_node,
                                                   const std::shared_ptr<ConfigNode>& override_node) {
        if (base_node->is_value() && override_node->is_value()) {
            auto* base_val = base_node->as_value();
            auto* override_val = override_node->as_value();

            ScalarValue num_res{};
            if (try_perform_numeric_op(base_val, override_val, NumericOp::Divide, num_res)) {
                return std::make_shared<ConfigValueNode>(num_res, ConfigOperator::Assign, override_node->source());
            }
        }

        throw InvalidOperatorException("/=",
                                       std::string(to_string(base_node->node_type())),
                                       "Cannot divide " + std::string(to_string(base_node->node_type())) + " by " + std::string(to_string(override_node->node_type())),
                                       override_node->source());
    }

    static std::shared_ptr<ConfigNode> apply_bitwise_and(const std::shared_ptr<ConfigNode>& base_node,
                                                        const std::shared_ptr<ConfigNode>& override_node) {
        if (base_node->is_value() && override_node->is_value()) {
            auto* base_val = base_node->as_value();
            auto* override_val = override_node->as_value();

            if (base_val->is_bool() && override_val->is_bool()) {
                return std::make_shared<ConfigValueNode>(base_val->as_bool() && override_val->as_bool(),
                                                        ConfigOperator::Assign, override_node->source());
            }
            if (base_val->is_integer() && override_val->is_integer()) {
                return std::make_shared<ConfigValueNode>(base_val->as_integer() & override_val->as_integer(),
                                                        ConfigOperator::Assign, override_node->source());
            }
        }

        throw InvalidOperatorException("&=",
                                       std::string(to_string(base_node->node_type())),
                                       "Bitwise AND is only supported for booleans and integers.",
                                       override_node->source());
    }

    static std::shared_ptr<ConfigNode> apply_bitwise_or(const std::shared_ptr<ConfigNode>& base_node,
                                                       const std::shared_ptr<ConfigNode>& override_node) {
        if (base_node->is_value() && override_node->is_value()) {
            auto* base_val = base_node->as_value();
            auto* override_val = override_node->as_value();

            if (base_val->is_bool() && override_val->is_bool()) {
                return std::make_shared<ConfigValueNode>(base_val->as_bool() || override_val->as_bool(),
                                                        ConfigOperator::Assign, override_node->source());
            }
            if (base_val->is_integer() && override_val->is_integer()) {
                return std::make_shared<ConfigValueNode>(base_val->as_integer() | override_val->as_integer(),
                                                        ConfigOperator::Assign, override_node->source());
            }
        }

        throw InvalidOperatorException("|=",
                                       std::string(to_string(base_node->node_type())),
                                       "Bitwise OR is only supported for booleans and integers.",
                                       override_node->source());
    }

    static std::shared_ptr<ConfigNode> apply_toggle(const std::shared_ptr<ConfigNode>& base_node,
                                                   const std::shared_ptr<ConfigNode>& override_node) {
        if (base_node->is_value()) {
            auto* base_val = base_node->as_value();
            if (base_val->is_bool()) {
                return std::make_shared<ConfigValueNode>(!base_val->as_bool(),
                                                        ConfigOperator::Assign, override_node->source());
            }
        }

        throw InvalidOperatorException("!=",
                                       std::string(to_string(base_node->node_type())),
                                       "Toggle operator (!=) is only supported for booleans.",
                                       override_node->source());
    }

    static bool try_perform_numeric_op(const ConfigValueNode* a,
                                       const ConfigValueNode* b,
                                       NumericOp op,
                                       ScalarValue& out_result) {
        if (!a->is_number() || !b->is_number()) return false;

        bool a_is_double = a->is_double();
        bool b_is_double = b->is_double();

        if (a_is_double || b_is_double) {
            double da = a->as_double();
            double db = b->as_double();
            out_result = apply_double_op(da, db, op);
            return true;
        } else {
            int64_t la = a->as_integer();
            int64_t lb = b->as_integer();
            out_result = apply_int64_op(la, lb, op);
            return true;
        }
    }

    static int64_t apply_int64_op(int64_t a, int64_t b, NumericOp op) {
        switch (op) {
            case NumericOp::Add: {
                int64_t result = a + b;
                if ((a > 0 && b > 0 && result < 0) || (a < 0 && b < 0 && result > 0)) {
                    throw ConfigOverflowException("Integer overflow in configuration addition.");
                }
                return result;
            }
            case NumericOp::Subtract: {
                int64_t result = a - b;
                if ((a > 0 && b < 0 && result < 0) || (a < 0 && b > 0 && result > 0)) {
                    throw ConfigOverflowException("Integer overflow in configuration subtraction.");
                }
                return result;
            }
            case NumericOp::Multiply: {
                if (a != 0 && b != 0 && (a * b) / a != b) {
                    throw ConfigOverflowException("Integer overflow in configuration multiplication.");
                }
                return a * b;
            }
            case NumericOp::Divide: {
                if (b == 0) {
                    throw ConfigDivideByZeroException("Configuration division by zero.");
                }
                if (a == std::numeric_limits<int64_t>::min() && b == -1) {
                    throw ConfigOverflowException("Integer overflow in configuration division.");
                }
                return a / b;
            }
        }
        return 0;
    }

    static double apply_double_op(double a, double b, NumericOp op) {
        switch (op) {
            case NumericOp::Add: return a + b;
            case NumericOp::Subtract: return a - b;
            case NumericOp::Multiply: return a * b;
            case NumericOp::Divide: {
                if (b == 0.0) {
                    throw ConfigDivideByZeroException("Configuration division by zero.");
                }
                return a / b;
            }
        }
        return 0.0;
    }
};

} // namespace omegaconf
