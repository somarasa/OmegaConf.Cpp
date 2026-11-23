#pragma once

#include "omegaconf/ast/config_enums.hpp"
#include "omegaconf/common/exceptions.hpp"
#include <string>
#include <string_view>
#include <tuple>
#include <array>
#include <cctype>

namespace omegaconf {

/**
 * @brief Extracts mutation operators (+=, -=, *=, /=, &=, |=, !=, =)
 * from configuration key suffixes and command-line override expressions.
 */
class OperatorParser {
public:
    struct KeyOpResult {
        std::string clean_key;
        ConfigOperator op;
    };

    struct AssignmentResult {
        std::string path;
        ConfigOperator op;
        std::string value;
    };

    /**
     * @brief Parses a key name and checks if it contains an operator suffix (e.g. "port+=" -> {"port", ConfigOperator::Add}).
     */
    static KeyOpResult parse_key(std::string_view raw_key) {
        std::string_view trimmed = trim(raw_key);
        if (trimmed.empty()) {
            return {std::string(raw_key), ConfigOperator::Assign};
        }

        // Compound 2-character operators must precede simple '=' so they match greedily first.
        static constexpr std::array<std::pair<std::string_view, ConfigOperator>, 8> suffix_operators = {{
            {"+=", ConfigOperator::Add},
            {"-=", ConfigOperator::Subtract},
            {"*=", ConfigOperator::Multiply},
            {"/=", ConfigOperator::Divide},
            {"&=", ConfigOperator::BitwiseAnd},
            {"|=", ConfigOperator::BitwiseOr},
            {"!=", ConfigOperator::Toggle},
            {"=", ConfigOperator::Assign}
        }};

        for (const auto& [symbol, op] : suffix_operators) {
            if (ends_with(trimmed, symbol)) {
                std::string_view clean = trimmed.substr(0, trimmed.size() - symbol.size());
                clean = trim_right(clean);
                if (!clean.empty()) {
                    return {std::string(clean), op};
                }
            }
        }

        return {std::string(trimmed), ConfigOperator::Assign};
    }

    /**
     * @brief Parses an expression string like "app.timeout+=10" or "db=postgres" from CLI args.
     */
    static AssignmentResult parse_assignment(std::string_view raw_expr) {
        std::string_view trimmed = trim(raw_expr);
        if (trimmed.empty()) {
            throw ConfigSyntaxException("CLI expression cannot be empty.");
        }

        static constexpr std::array<std::pair<std::string_view, ConfigOperator>, 8> suffix_operators = {{
            {"+=", ConfigOperator::Add},
            {"-=", ConfigOperator::Subtract},
            {"*=", ConfigOperator::Multiply},
            {"/=", ConfigOperator::Divide},
            {"&=", ConfigOperator::BitwiseAnd},
            {"|=", ConfigOperator::BitwiseOr},
            {"!=", ConfigOperator::Toggle},
            {"=", ConfigOperator::Assign}
        }};

        size_t best_idx = std::string_view::npos;
        std::string_view best_symbol{};
        ConfigOperator best_op{ConfigOperator::Assign};

        for (const auto& [symbol, op] : suffix_operators) {
            size_t idx = trimmed.find(symbol);
            if (idx != std::string_view::npos && idx > 0 && idx < best_idx) {
                best_idx = idx;
                best_symbol = symbol;
                best_op = op;
            }
        }

        if (best_idx != std::string_view::npos) {
            std::string_view path = trim(trimmed.substr(0, best_idx));
            std::string_view val = trim(trimmed.substr(best_idx + best_symbol.size()));
            return {std::string(path), best_op, std::string(val)};
        }

        throw ConfigSyntaxException("Expression '" + std::string(raw_expr) + "' does not contain a valid operator (=, +=, -=, *=, /=, &=, |=, !=).");
    }

private:
    static std::string_view trim_left(std::string_view s) noexcept {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
            s.remove_prefix(1);
        }
        return s;
    }

    static std::string_view trim_right(std::string_view s) noexcept {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
            s.remove_suffix(1);
        }
        return s;
    }

    static std::string_view trim(std::string_view s) noexcept {
        return trim_right(trim_left(s));
    }
};

} // namespace omegaconf
