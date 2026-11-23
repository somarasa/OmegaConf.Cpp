#pragma once

#include <string_view>

namespace omegaconf {

/**
 * @brief Represents the structural category of a ConfigNode in the configuration Abstract Syntax Tree (AST).
 */
enum class ConfigNodeType {
    Value,  ///< A leaf scalar value node (string, integer, double, boolean, or null).
    List,   ///< An ordered sequence / list of child ConfigNode elements.
    Object  ///< A key-value dictionary / mapping of child ConfigNode elements.
};

/**
 * @brief Defines the mutation operator associated with a configuration key or CLI override expression.
 */
enum class ConfigOperator {
    Assign,      ///< = direct assignment / override
    Add,         ///< += addition, concatenation, or list append
    Subtract,    ///< -= subtraction or list item removal
    Multiply,    ///< *= multiplication or string repetition
    Divide,      ///< /= division
    BitwiseAnd,  ///< &= logical AND or bitwise AND
    BitwiseOr,   ///< |= logical OR or bitwise OR
    Toggle       ///< != logical toggle / invert
};

[[nodiscard]] constexpr std::string_view to_string(ConfigNodeType type) noexcept {
    switch (type) {
        case ConfigNodeType::Value: return "Value";
        case ConfigNodeType::List: return "List";
        case ConfigNodeType::Object: return "Object";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string(ConfigOperator op) noexcept {
    switch (op) {
        case ConfigOperator::Assign: return "=";
        case ConfigOperator::Add: return "+=";
        case ConfigOperator::Subtract: return "-=";
        case ConfigOperator::Multiply: return "*=";
        case ConfigOperator::Divide: return "/=";
        case ConfigOperator::BitwiseAnd: return "&=";
        case ConfigOperator::BitwiseOr: return "|=";
        case ConfigOperator::Toggle: return "!=";
    }
    return "=";
}

} // namespace omegaconf
