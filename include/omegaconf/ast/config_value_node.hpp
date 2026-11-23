#pragma once

#include "omegaconf/ast/config_node.hpp"
#include <variant>
#include <string>
#include <string_view>
#include <type_traits>
#include <iomanip>
#include <sstream>

namespace omegaconf {

using ScalarValue = std::variant<std::nullptr_t, bool, int64_t, double, std::string>;

/**
 * @brief Represents a scalar configuration value (string, integer, double, boolean, null, or mandatory marker '???').
 */
class ConfigValueNode : public ConfigNode {
public:
    static constexpr std::string_view MissingMarker = "???";

    ConfigValueNode()
        : value_(nullptr) {}

    explicit ConfigValueNode(std::nullptr_t, ConfigOperator op = ConfigOperator::Assign, SourceInfo source = SourceInfo::unknown())
        : value_(nullptr) {
        op_ = op;
        source_ = std::move(source);
    }

    explicit ConfigValueNode(bool val, ConfigOperator op = ConfigOperator::Assign, SourceInfo source = SourceInfo::unknown())
        : value_(val) {
        op_ = op;
        source_ = std::move(source);
    }

    template <typename Int, std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>, int> = 0>
    explicit ConfigValueNode(Int val, ConfigOperator op = ConfigOperator::Assign, SourceInfo source = SourceInfo::unknown())
        : value_(static_cast<int64_t>(val)) {
        op_ = op;
        source_ = std::move(source);
    }

    template <typename Float, std::enable_if_t<std::is_floating_point_v<Float>, int> = 0>
    explicit ConfigValueNode(Float val, ConfigOperator op = ConfigOperator::Assign, SourceInfo source = SourceInfo::unknown())
        : value_(static_cast<double>(val)) {
        op_ = op;
        source_ = std::move(source);
    }

    explicit ConfigValueNode(std::string str, ConfigOperator op = ConfigOperator::Assign, SourceInfo source = SourceInfo::unknown()) {
        op_ = op;
        source_ = std::move(source);
        init_string(std::move(str));
    }

    explicit ConfigValueNode(std::string_view str, ConfigOperator op = ConfigOperator::Assign, SourceInfo source = SourceInfo::unknown())
        : ConfigValueNode(std::string(str), op, std::move(source)) {}

    explicit ConfigValueNode(const char* str, ConfigOperator op = ConfigOperator::Assign, SourceInfo source = SourceInfo::unknown())
        : ConfigValueNode(std::string(str ? str : ""), op, std::move(source)) {}

    explicit ConfigValueNode(ScalarValue val, ConfigOperator op = ConfigOperator::Assign, SourceInfo source = SourceInfo::unknown()) {
        op_ = op;
        source_ = std::move(source);
        if (std::holds_alternative<std::string>(val)) {
            init_string(std::get<std::string>(std::move(val)));
        } else {
            value_ = std::move(val);
        }
    }

    [[nodiscard]] ConfigNodeType node_type() const noexcept override {
        return ConfigNodeType::Value;
    }

    [[nodiscard]] const ScalarValue& value() const noexcept {
        return value_;
    }

    void set_value(ScalarValue val) {
        ensure_writable();
        if (const auto* str = std::get_if<std::string>(&val)) {
            if (str->find("${") != std::string::npos && raw_template_.empty()) {
                raw_template_ = *str;
            }
        }
        value_ = std::move(val);
    }

    [[nodiscard]] bool is_null() const noexcept {
        return std::holds_alternative<std::nullptr_t>(value_);
    }

    [[nodiscard]] bool is_bool() const noexcept {
        return std::holds_alternative<bool>(value_);
    }

    [[nodiscard]] bool is_integer() const noexcept {
        return std::holds_alternative<int64_t>(value_);
    }

    [[nodiscard]] bool is_double() const noexcept {
        return std::holds_alternative<double>(value_);
    }

    [[nodiscard]] bool is_number() const noexcept {
        return is_integer() || is_double();
    }

    [[nodiscard]] bool is_string() const noexcept {
        return std::holds_alternative<std::string>(value_);
    }

    [[nodiscard]] bool as_bool() const {
        if (is_bool()) return std::get<bool>(value_);
        if (is_integer()) return std::get<int64_t>(value_) != 0;
        if (is_double()) return std::get<double>(value_) != 0.0;
        if (is_string()) {
            const auto& s = std::get<std::string>(value_);
            if (s == "true" || s == "True" || s == "TRUE" || s == "1") return true;
            if (s == "false" || s == "False" || s == "FALSE" || s == "0") return false;
        }
        throw ConfigTypeMismatchException(source_.to_string(), "bool", to_string(), source_);
    }

    [[nodiscard]] int64_t as_integer() const {
        if (is_integer()) return std::get<int64_t>(value_);
        if (is_double()) return static_cast<int64_t>(std::get<double>(value_));
        if (is_bool()) return std::get<bool>(value_) ? 1 : 0;
        if (is_string()) {
            try {
                return std::stoll(std::get<std::string>(value_));
            } catch (...) {
                throw ConfigTypeMismatchException(source_.to_string(), "integer", to_string(), source_);
            }
        }
        throw ConfigTypeMismatchException(source_.to_string(), "integer", to_string(), source_);
    }

    [[nodiscard]] double as_double() const {
        if (is_double()) return std::get<double>(value_);
        if (is_integer()) return static_cast<double>(std::get<int64_t>(value_));
        if (is_bool()) return std::get<bool>(value_) ? 1.0 : 0.0;
        if (is_string()) {
            try {
                return std::stod(std::get<std::string>(value_));
            } catch (...) {
                throw ConfigTypeMismatchException(source_.to_string(), "double", to_string(), source_);
            }
        }
        throw ConfigTypeMismatchException(source_.to_string(), "double", to_string(), source_);
    }

    [[nodiscard]] std::string as_string() const {
        if (is_string()) return std::get<std::string>(value_);
        return to_string();
    }

    [[nodiscard]] const std::string& raw_template() const noexcept {
        return raw_template_;
    }

    void set_raw_template(std::string tmpl) {
        raw_template_ = std::move(tmpl);
    }

    [[nodiscard]] bool is_escaped_literal() const noexcept {
        return is_escaped_literal_;
    }

    void set_escaped_literal(bool escaped) noexcept {
        is_escaped_literal_ = escaped;
    }

    [[nodiscard]] bool is_missing_marker() const noexcept {
        if (is_escaped_literal_) return false;
        if (const auto* s = std::get_if<std::string>(&value_)) {
            return *s == MissingMarker;
        }
        return false;
    }

    [[nodiscard]] std::shared_ptr<ConfigNode> clone() const override {
        auto node = std::make_shared<ConfigValueNode>(value_, op_, source_);
        node->raw_template_ = raw_template_;
        node->is_escaped_literal_ = is_escaped_literal_;
        return node;
    }

    [[nodiscard]] std::string to_string() const override {
        if (is_missing_marker()) {
            return std::string(MissingMarker);
        }
        return std::visit([](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                return "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream ss;
                ss << v;
                return ss.str();
            } else if constexpr (std::is_same_v<T, std::string>) {
                return v;
            }
        }, value_);
    }

private:
    ScalarValue value_{nullptr};
    std::string raw_template_{};
    bool is_escaped_literal_{false};

    void init_string(std::string raw_str) {
        if (raw_str == "\\???") {
            value_ = std::string(MissingMarker);
            is_escaped_literal_ = true;
            raw_template_.clear();
        } else if (raw_str == "\\\\???") {
            value_ = std::string("\\???");
            is_escaped_literal_ = true;
            raw_template_.clear();
        } else {
            if (raw_str.find("${") != std::string::npos) {
                raw_template_ = raw_str;
            } else {
                raw_template_.clear();
            }
            value_ = std::move(raw_str);
            is_escaped_literal_ = false;
        }
    }
};

} // namespace omegaconf
