#pragma once

#include "omegaconf/interpolation/resolver.hpp"
#include "omegaconf/parsers/yaml_format_parser.hpp"
#include "omegaconf/common/exceptions.hpp"
#include <string>
#include <string_view>
#include <functional>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <sstream>
#include <cctype>
#include <variant>
#include <algorithm>
#include <cmath>

namespace omegaconf {

/**
 * @brief User-defined custom resolver delegating to a std::function.
 */
class CustomResolver : public IResolver {
public:
    using ResolverFunc = std::function<std::string(std::string_view)>;

    CustomResolver(std::string name, ResolverFunc func)
        : name_(std::move(name)), func_(std::move(func)) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] std::string resolve(std::string_view argument, IInterpolationContext&) override {
        return func_(argument);
    }

private:
    std::string name_{};
    ResolverFunc func_{};
};

/**
 * @brief Environment variable resolver (${env:VAR} or ${env:VAR, default}).
 */
class EnvResolver : public IResolver {
public:
    explicit EnvResolver(std::string name = "env")
        : name_(std::move(name)) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] std::string resolve(std::string_view argument, IInterpolationContext&) override {
        std::string_view var_name = argument;
        std::string default_val{};
        bool has_default = false;

        size_t comma = argument.find(',');
        if (comma != std::string_view::npos) {
            var_name = trim(argument.substr(0, comma));
            default_val = std::string(trim(argument.substr(comma + 1)));
            has_default = true;
        }

        std::string var_name_str(var_name);
#if defined(_MSC_VER)
        char* buf = nullptr;
        size_t sz = 0;
        if (_dupenv_s(&buf, &sz, var_name_str.c_str()) == 0 && buf != nullptr) {
            std::string val(buf);
            free(buf);
            if (!val.empty()) return val;
        }
#else
        const char* env_val = std::getenv(var_name_str.c_str());
        if (env_val && *env_val) {
            return std::string(env_val);
        }
#endif

        if (has_default) {
            return default_val;
        }

        throw ConfigInterpolationException("Environment variable '" + var_name_str + "' is not set and no default value was provided.");
    }

private:
    std::string name_{};

    static std::string_view trim(std::string_view s) noexcept {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    }
};

/**
 * @brief Timestamp resolver (${now:format}).
 */
class NowResolver : public IResolver {
public:
    explicit NowResolver(std::string name = "now")
        : name_(std::move(name)) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] std::string resolve(std::string_view argument, IInterpolationContext&) override {
        std::string fmt = argument.empty() ? "%Y-%m-%d" : convert_format(argument);
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#if defined(_MSC_VER)
        localtime_s(&tm_buf, &tt);
#else
        localtime_r(&tt, &tm_buf);
#endif
        char buf[128];
        if (std::strftime(buf, sizeof(buf), fmt.c_str(), &tm_buf) > 0) {
            return std::string(buf);
        }
        return "";
    }

private:
    std::string name_{};

    static std::string convert_format(std::string_view fmt) {
        std::string s(fmt);
        // Convert .NET format specifiers if used (yyyy-MM-dd -> %Y-%m-%d)
        replace_all(s, "yyyy", "%Y");
        replace_all(s, "MM", "%m");
        replace_all(s, "dd", "%d");
        replace_all(s, "HH", "%H");
        replace_all(s, "mm", "%M");
        replace_all(s, "ss", "%S");
        return s;
    }

    static void replace_all(std::string& str, std::string_view from, std::string_view to) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
};

/**
 * @brief Built-in OmegaConf decoder resolver (oc.decode).
 */
class OcDecodeResolver : public IResolver {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "oc.decode";
    }

    [[nodiscard]] std::string resolve(std::string_view argument, IInterpolationContext& context) override {
        auto opt = resolve_typed(argument, context);
        if (!opt.has_value()) return "";
        const auto& val = *opt;
        if (std::holds_alternative<std::shared_ptr<ConfigNode>>(val)) {
            auto n = std::get<std::shared_ptr<ConfigNode>>(val);
            return n ? n->to_string() : "null";
        }
        return std::visit([](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) return "null";
            if constexpr (std::is_same_v<T, bool>) return v ? "True" : "False";
            if constexpr (std::is_same_v<T, int64_t>) return std::to_string(v);
            if constexpr (std::is_same_v<T, double>) {
                std::ostringstream ss;
                ss << v;
                return ss.str();
            }
            if constexpr (std::is_same_v<T, std::string>) return v;
        }, std::get<ScalarValue>(val));
    }

    [[nodiscard]] std::optional<ResolvedValue> resolve_typed(
        std::string_view argument,
        IInterpolationContext& /*context*/) override {
        return decode_value(argument);
    }

    static ResolvedValue decode_value(std::string_view raw) {
        std::string_view trimmed = trim(raw);
        if (trimmed.empty() || trimmed == "null" || trimmed == "~") {
            return ScalarValue(nullptr);
        }

        std::string lower(trimmed);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (lower == "true" || lower == "yes" || lower == "on") {
            return ScalarValue(true);
        }
        if (lower == "false" || lower == "no" || lower == "off") {
            return ScalarValue(false);
        }

        // Try integer
        try {
            size_t idx = 0;
            int64_t val = std::stoll(std::string(trimmed), &idx, 0);
            if (idx == trimmed.size()) {
                return ScalarValue(val);
            }
        } catch (...) {}

        // Try double
        try {
            size_t idx = 0;
            double val = std::stod(std::string(trimmed), &idx);
            if (idx == trimmed.size()) {
                return ScalarValue(val);
            }
        } catch (...) {}

        // List or Mapping
        if ((starts_with(trimmed, '[') && ends_with(trimmed, ']')) ||
            (starts_with(trimmed, '{') && ends_with(trimmed, '}'))) {
            try {
                auto parsed = YamlFormatParser::parse("_decoded_: " + std::string(trimmed));
                auto child = parsed->get_child("_decoded_");
                if (child) {
                    return child;
                }
            } catch (...) {}
        }

        // Strip quotes
        if (trimmed.size() >= 2 &&
            ((starts_with(trimmed, '"') && ends_with(trimmed, '"')) ||
             (starts_with(trimmed, '\'') && ends_with(trimmed, '\'')))) {
            return ScalarValue(std::string(trimmed.substr(1, trimmed.size() - 2)));
        }

        return ScalarValue(std::string(trimmed));
    }

private:
    static std::string_view trim(std::string_view s) noexcept {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    }
};

/**
 * @brief Built-in OmegaConf safe selector resolver (oc.select).
 */
class OcSelectResolver : public IResolver {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "oc.select";
    }

    [[nodiscard]] std::string resolve(std::string_view argument, IInterpolationContext& context) override {
        auto opt = resolve_typed(argument, context);
        if (!opt.has_value()) return "";
        const auto& val = *opt;
        if (std::holds_alternative<std::shared_ptr<ConfigNode>>(val)) {
            auto n = std::get<std::shared_ptr<ConfigNode>>(val);
            return n ? n->to_string() : "";
        }
        return std::visit([](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) return "";
            if constexpr (std::is_same_v<T, bool>) return v ? "True" : "False";
            if constexpr (std::is_same_v<T, int64_t>) return std::to_string(v);
            if constexpr (std::is_same_v<T, double>) {
                std::ostringstream ss;
                ss << v;
                return ss.str();
            }
            if constexpr (std::is_same_v<T, std::string>) return v;
        }, std::get<ScalarValue>(val));
    }

    [[nodiscard]] std::optional<ResolvedValue> resolve_typed(
        std::string_view argument,
        IInterpolationContext& context) override {
        std::string_view path = argument;
        std::string fallback_raw{};
        bool has_fallback = false;

        size_t comma = argument.find(',');
        if (comma != std::string_view::npos) {
            path = trim(argument.substr(0, comma));
            fallback_raw = std::string(trim(argument.substr(comma + 1)));
            has_fallback = true;
        }

        auto node = context.resolve_node(path);
        if (node) {
            if (node->is_value()) {
                auto* vn = node->as_value();
                if (!vn->is_missing_marker()) {
                    return vn->value();
                }
            } else {
                return node;
            }
        }

        if (has_fallback) {
            return OcDecodeResolver::decode_value(fallback_raw);
        }

        return ScalarValue(nullptr);
    }

private:
    static std::string_view trim(std::string_view s) noexcept {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    }
};

/**
 * @brief Built-in OmegaConf dictionary keys resolver (oc.dict.keys).
 */
class OcDictKeysResolver : public IResolver {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "oc.dict.keys";
    }

    [[nodiscard]] std::string resolve(std::string_view argument, IInterpolationContext& context) override {
        auto opt = resolve_typed(argument, context);
        if (!opt.has_value()) return "[]";
        auto node = std::get<std::shared_ptr<ConfigNode>>(*opt);
        return node ? node->to_string() : "[]";
    }

    [[nodiscard]] std::optional<ResolvedValue> resolve_typed(
        std::string_view argument,
        IInterpolationContext& context) override {
        std::string_view path = trim(argument);
        auto node = context.resolve_node(path);
        if (node && node->is_object()) {
            auto* obj = node->as_object();
            auto list = std::make_shared<ConfigListNode>();
            for (const auto& k : obj->keys()) {
                list->add_item(std::make_shared<ConfigValueNode>(k));
            }
            return std::static_pointer_cast<ConfigNode>(list);
        }

        throw ConfigInterpolationException("Resolver 'oc.dict.keys' requires path '" + std::string(path) + "' to reference a mapping/object node.");
    }

private:
    static std::string_view trim(std::string_view s) noexcept {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    }
};

/**
 * @brief Math, logic, and comparison expression evaluator (${eval:...}).
 */
class EvalResolver : public IResolver {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "eval";
    }

    [[nodiscard]] std::string resolve(std::string_view argument, IInterpolationContext&) override {
        if (argument.empty()) return "";
        Parser parser(argument);
        Value result = parser.parse();
        return to_result_string(result);
    }

private:
    using Value = std::variant<bool, int64_t, double, std::string>;

    static std::string to_result_string(const Value& val) {
        return std::visit([](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                return v ? "True" : "False";
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream ss;
                ss << v;
                return ss.str();
            } else if constexpr (std::is_same_v<T, std::string>) {
                return v;
            }
        }, val);
    }

    class Parser {
    public:
        explicit Parser(std::string_view expr) : expr_(expr) {}

        Value parse() {
            Value res = parse_logical_or();
            skip_whitespace();
            if (pos_ < expr_.size()) {
                throw ConfigSyntaxException("Unexpected character in eval expression at position " +
                                            std::to_string(pos_) + ": '" + std::string(1, expr_[pos_]) + "'");
            }
            return res;
        }

    private:
        std::string_view expr_;
        size_t pos_{0};

        void skip_whitespace() noexcept {
            while (pos_ < expr_.size() && std::isspace(static_cast<unsigned char>(expr_[pos_]))) {
                ++pos_;
            }
        }

        bool match(std::string_view s) noexcept {
            skip_whitespace();
            if (starts_with(expr_.substr(pos_), s)) {
                pos_ += s.size();
                return true;
            }
            return false;
        }

        Value parse_logical_or() {
            Value left = parse_logical_and();
            while (match("||")) {
                Value right = parse_logical_and();
                left = to_bool(left) || to_bool(right);
            }
            return left;
        }

        Value parse_logical_and() {
            Value left = parse_equality();
            while (match("&&")) {
                Value right = parse_equality();
                left = to_bool(left) && to_bool(right);
            }
            return left;
        }

        Value parse_equality() {
            Value left = parse_relational();
            while (true) {
                if (match("==")) {
                    Value right = parse_relational();
                    left = are_equal(left, right);
                } else if (match("!=")) {
                    Value right = parse_relational();
                    left = !are_equal(left, right);
                } else {
                    break;
                }
            }
            return left;
        }

        Value parse_relational() {
            Value left = parse_additive();
            while (true) {
                if (match("<=")) {
                    Value right = parse_additive();
                    left = to_double(left) <= to_double(right);
                } else if (match(">=")) {
                    Value right = parse_additive();
                    left = to_double(left) >= to_double(right);
                } else if (match("<")) {
                    Value right = parse_additive();
                    left = to_double(left) < to_double(right);
                } else if (match(">")) {
                    Value right = parse_additive();
                    left = to_double(left) > to_double(right);
                } else {
                    break;
                }
            }
            return left;
        }

        Value parse_additive() {
            Value left = parse_multiplicative();
            while (true) {
                if (match("+")) {
                    Value right = parse_multiplicative();
                    if (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right)) {
                        left = to_string(left) + to_string(right);
                    } else if (std::holds_alternative<double>(left) || std::holds_alternative<double>(right)) {
                        left = to_double(left) + to_double(right);
                    } else {
                        left = to_int(left) + to_int(right);
                    }
                } else if (match("-")) {
                    Value right = parse_multiplicative();
                    if (std::holds_alternative<double>(left) || std::holds_alternative<double>(right)) {
                        left = to_double(left) - to_double(right);
                    } else {
                        left = to_int(left) - to_int(right);
                    }
                } else {
                    break;
                }
            }
            return left;
        }

        Value parse_multiplicative() {
            Value left = parse_unary();
            while (true) {
                if (match("*")) {
                    Value right = parse_unary();
                    if (std::holds_alternative<double>(left) || std::holds_alternative<double>(right)) {
                        left = to_double(left) * to_double(right);
                    } else {
                        left = to_int(left) * to_int(right);
                    }
                } else if (match("/")) {
                    Value right = parse_unary();
                    double r = to_double(right);
                    if (r == 0.0) throw ConfigSyntaxException("Division by zero in eval expression.");
                    if (!std::holds_alternative<double>(left) && !std::holds_alternative<double>(right)) {
                        int64_t li = to_int(left);
                        int64_t ri = to_int(right);
                        if (li % ri == 0) {
                            left = li / ri;
                            continue;
                        }
                    }
                    left = to_double(left) / r;
                } else if (match("%")) {
                    Value right = parse_unary();
                    double r = to_double(right);
                    if (r == 0.0) throw ConfigSyntaxException("Modulo by zero in eval expression.");
                    if (!std::holds_alternative<double>(left) && !std::holds_alternative<double>(right)) {
                        left = to_int(left) % to_int(right);
                    } else {
                        left = std::fmod(to_double(left), r);
                    }
                } else {
                    break;
                }
            }
            return left;
        }

        Value parse_unary() {
            skip_whitespace();
            if (match("!")) {
                return !to_bool(parse_unary());
            }
            if (match("-")) {
                Value val = parse_unary();
                if (std::holds_alternative<double>(val)) return -std::get<double>(val);
                return -to_int(val);
            }
            if (match("+")) {
                return parse_unary();
            }
            return parse_primary();
        }

        Value parse_primary() {
            skip_whitespace();
            if (pos_ >= expr_.size()) {
                throw ConfigSyntaxException("Unexpected end of eval expression.");
            }

            if (match("(")) {
                Value val = parse_logical_or();
                if (!match(")")) {
                    throw ConfigSyntaxException("Missing closing parenthesis in eval expression.");
                }
                return val;
            }

            // String literal: '...' or "..."
            char quote = expr_[pos_];
            if (quote == '\'' || quote == '"') {
                ++pos_;
                size_t start = pos_;
                while (pos_ < expr_.size() && expr_[pos_] != quote) {
                    ++pos_;
                }
                if (pos_ >= expr_.size()) {
                    throw ConfigSyntaxException("Unterminated string literal in eval expression.");
                }
                std::string s(expr_.substr(start, pos_ - start));
                ++pos_;
                return s;
            }

            // Booleans
            if (starts_with(expr_.substr(pos_), "true") || starts_with(expr_.substr(pos_), "True")) {
                pos_ += 4;
                return true;
            }
            if (starts_with(expr_.substr(pos_), "false") || starts_with(expr_.substr(pos_), "False")) {
                pos_ += 5;
                return false;
            }

            // Numbers
            size_t start = pos_;
            bool has_dot = false;
            while (pos_ < expr_.size() && (std::isdigit(static_cast<unsigned char>(expr_[pos_])) || expr_[pos_] == '.')) {
                if (expr_[pos_] == '.') has_dot = true;
                ++pos_;
            }

            if (pos_ > start) {
                std::string num_str(expr_.substr(start, pos_ - start));
                if (has_dot) {
                    return std::stod(num_str);
                }
                return std::stoll(num_str);
            }

            throw ConfigSyntaxException("Unexpected token in eval expression at position " +
                                        std::to_string(pos_) + ": '" + std::string(1, expr_[pos_]) + "'");
        }

        static bool to_bool(const Value& val) noexcept {
            return std::visit([](const auto& v) -> bool {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>) return v;
                if constexpr (std::is_same_v<T, int64_t>) return v != 0;
                if constexpr (std::is_same_v<T, double>) return v != 0.0;
                if constexpr (std::is_same_v<T, std::string>) return !v.empty() && v != "false" && v != "False";
            }, val);
        }

        static int64_t to_int(const Value& val) {
            return std::visit([](const auto& v) -> int64_t {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, int64_t>) return v;
                if constexpr (std::is_same_v<T, double>) return static_cast<int64_t>(v);
                if constexpr (std::is_same_v<T, bool>) return v ? 1 : 0;
                if constexpr (std::is_same_v<T, std::string>) return std::stoll(v);
            }, val);
        }

        static double to_double(const Value& val) {
            return std::visit([](const auto& v) -> double {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, double>) return v;
                if constexpr (std::is_same_v<T, int64_t>) return static_cast<double>(v);
                if constexpr (std::is_same_v<T, bool>) return v ? 1.0 : 0.0;
                if constexpr (std::is_same_v<T, std::string>) return std::stod(v);
            }, val);
        }

        static std::string to_string(const Value& val) {
            return to_result_string(val);
        }

        static bool are_equal(const Value& a, const Value& b) noexcept {
            if (a.index() == b.index()) {
                return a == b;
            }
            if ((std::holds_alternative<int64_t>(a) || std::holds_alternative<double>(a)) &&
                (std::holds_alternative<int64_t>(b) || std::holds_alternative<double>(b))) {
                return to_double(a) == to_double(b);
            }
            return false;
        }
    };
};

} // namespace omegaconf
