#pragma once

#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include "omegaconf/ast/config_list_node.hpp"
#include "omegaconf/ast/config_object_node.hpp"
#include "omegaconf/interpolation/resolver.hpp"
#include "omegaconf/interpolation/resolvers.hpp"
#include "omegaconf/common/exceptions.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <regex>

namespace omegaconf {

class InterpolationEngine : public IInterpolationContext {
public:
    explicit InterpolationEngine(std::shared_ptr<ConfigObjectNode> root,
                                 std::vector<std::shared_ptr<IResolver>> custom_resolvers = {})
        : root_(std::move(root)) {
        register_resolver(std::make_shared<EnvResolver>("env"));
        register_resolver(std::make_shared<EnvResolver>("oc.env"));
        register_resolver(std::make_shared<EvalResolver>());
        register_resolver(std::make_shared<NowResolver>());
        register_resolver(std::make_shared<OcSelectResolver>());
        register_resolver(std::make_shared<OcDecodeResolver>());
        register_resolver(std::make_shared<OcDictKeysResolver>());

        for (auto& r : custom_resolvers) {
            register_resolver(std::move(r));
        }
    }

    void register_resolver(std::shared_ptr<IResolver> resolver) {
        if (resolver) {
            std::string name(resolver->name());
            resolvers_[name] = std::move(resolver);
        }
    }

    void resolve_all() {
        if (root_) {
            resolve_node(root_, "");
        }
    }

    [[nodiscard]] std::shared_ptr<ConfigNode> resolve_node(std::string_view path) override {
        return root_ ? root_->select_node(path) : nullptr;
    }

    [[nodiscard]] std::string resolve_custom(std::string_view resolver_name, std::string_view argument) override {
        auto it = resolvers_.find(std::string(resolver_name));
        if (it != resolvers_.end() && it->second) {
            return it->second->resolve(argument, *this);
        }
        throw ConfigInterpolationException("Custom resolver '" + std::string(resolver_name) + "' is not registered.");
    }

    using ResolvedValue = std::variant<ScalarValue, std::shared_ptr<ConfigNode>>;

    ResolvedValue resolve_string_value(std::string_view raw, std::string_view current_path, const SourceInfo& source) {
        std::string current_path_str(current_path);

        if (!current_path_str.empty()) {
            if (resolving_keys_.find(current_path_str) != resolving_keys_.end()) {
                throw ConfigInterpolationException("Circular reference detected while resolving '" + current_path_str + "'.", source);
            }
            resolving_keys_.insert(current_path_str);
        }

        struct Guard {
            std::unordered_set<std::string, CaseInsensitiveHash, CaseInsensitiveEqual>& set;
            std::string key;
            ~Guard() {
                if (!key.empty()) set.erase(key);
            }
        } guard{resolving_keys_, current_path_str};

        std::string raw_str(raw);

        std::string outer_inner;
        if (is_single_outer_interpolation(raw_str, outer_inner)) {
            return resolve_expression(outer_inner, current_path_str, source);
        }

        // Embedded string interpolation (e.g. "http://${host}:${port}/api")
        std::string result = raw_str;
        static const std::regex interp_regex(R"((?:[^\\]|^)(\$\{([^{}]+)\}))");

        int max_iterations = 25;
        int iteration = 0;

        while (iteration++ < max_iterations) {
            std::smatch match;
            if (!std::regex_search(result, match, interp_regex)) {
                break;
            }

            size_t token_pos = match.position(1);
            size_t token_len = match.length(1);
            std::string inner = match.str(2);

            ResolvedValue res = resolve_expression(inner, current_path_str, source);
            std::string rep = format_resolved(res);

            result.replace(token_pos, token_len, rep);
        }

        // Unescape literal \${...} sequences into plain ${...}
        replace_all(result, "\\${", "${");

        return ScalarValue(result);
    }

    std::string resolve_string(std::string_view raw, std::string_view current_path = "", const SourceInfo& source = SourceInfo::unknown()) {
        auto val = resolve_string_value(raw, current_path, source);
        if (std::holds_alternative<ScalarValue>(val)) {
            ConfigValueNode tmp(std::get<ScalarValue>(val));
            return tmp.to_string();
        } else if (std::holds_alternative<std::shared_ptr<ConfigNode>>(val)) {
            auto n = std::get<std::shared_ptr<ConfigNode>>(val);
            return n ? n->to_string() : "";
        }
        return "";
    }

private:
    std::shared_ptr<ConfigObjectNode> root_{};
    std::unordered_map<std::string, std::shared_ptr<IResolver>, CaseInsensitiveHash, CaseInsensitiveEqual> resolvers_{};
    std::unordered_set<std::string, CaseInsensitiveHash, CaseInsensitiveEqual> resolving_keys_{};

    void resolve_node(const std::shared_ptr<ConfigNode>& node, const std::string& current_path) {
        if (!node) return;

        if (node->is_object()) {
            auto* obj = node->as_object();
            auto keys = obj->keys();
            for (const auto& key : keys) {
                auto child = obj->get_child(key);
                if (!child) continue;

                std::string child_path = current_path.empty() ? key : current_path + "." + key;
                if (child->is_value()) {
                    auto* child_val = child->as_value();
                    if (child_val->is_string()) {
                        const auto& str = child_val->as_string();
                        if (str.find("${") != std::string::npos || str.find("\\${") != std::string::npos) {
                            if (child_val->raw_template().empty()) {
                                child_val->set_raw_template(str);
                            }
                            ResolvedValue res = resolve_string_value(str, child_path, child_val->source());
                            if (std::holds_alternative<std::shared_ptr<ConfigNode>>(res)) {
                                auto rep = std::get<std::shared_ptr<ConfigNode>>(res)->clone();
                                obj->set_child(key, rep, true);
                                resolve_node(rep, child_path);
                            } else {
                                child_val->set_value(std::get<ScalarValue>(res));
                            }
                        }
                    }
                } else {
                    resolve_node(child, child_path);
                }
            }
        } else if (node->is_list()) {
            auto* list = node->as_list();
            for (size_t i = 0; i < list->size(); ++i) {
                auto item = (*list)[i];
                if (!item) continue;

                std::string item_path = current_path + "[" + std::to_string(i) + "]";
                if (item->is_value()) {
                    auto* item_val = item->as_value();
                    if (item_val->is_string()) {
                        const auto& str = item_val->as_string();
                        if (str.find("${") != std::string::npos || str.find("\\${") != std::string::npos) {
                            if (item_val->raw_template().empty()) {
                                item_val->set_raw_template(str);
                            }
                            ResolvedValue res = resolve_string_value(str, item_path, item_val->source());
                            if (std::holds_alternative<std::shared_ptr<ConfigNode>>(res)) {
                                auto rep = std::get<std::shared_ptr<ConfigNode>>(res)->clone();
                                (*list)[i] = rep;
                                resolve_node(rep, item_path);
                            } else {
                                item_val->set_value(std::get<ScalarValue>(res));
                            }
                        }
                    }
                } else {
                    resolve_node(item, item_path);
                }
            }
        }
    }

    ResolvedValue resolve_expression(std::string_view expr_view, const std::string& current_path, const SourceInfo& source) {
        std::string expr(expr_view);

        // Resolve inner nested interpolations first (e.g. "eval: ${batch_size} * ${num_gpus}")
        static const std::regex inner_regex(R"((?:[^\\]|^)(\$\{([^{}]+)\}))");
        int max_iterations = 25;
        int iteration = 0;
        while (iteration++ < max_iterations) {
            std::smatch match;
            if (!std::regex_search(expr, match, inner_regex)) break;

            size_t token_pos = match.position(1);
            size_t token_len = match.length(1);
            std::string inner = match.str(2);

            ResolvedValue res = resolve_expression(inner, current_path, source);
            std::string rep = format_resolved(res);
            expr.replace(token_pos, token_len, rep);
        }

        // Check if prefixed with a registered resolver (e.g. "env:PORT" or "eval: 10 * 20")
        size_t colon_pos = expr.find(':');
        if (colon_pos != std::string::npos && colon_pos > 0) {
            std::string prefix = expr.substr(0, colon_pos);
            while (!prefix.empty() && std::isspace(static_cast<unsigned char>(prefix.front()))) prefix.erase(prefix.begin());
            while (!prefix.empty() && std::isspace(static_cast<unsigned char>(prefix.back()))) prefix.pop_back();

            auto it = resolvers_.find(prefix);
            if (it != resolvers_.end() && it->second) {
                std::string arg = expr.substr(colon_pos + 1);
                while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.front()))) arg.erase(arg.begin());
                while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.back()))) arg.pop_back();

                auto typed_opt = it->second->resolve_typed(arg, *this);
                if (typed_opt.has_value()) {
                    return *typed_opt;
                }

                std::string resolved_str = it->second->resolve(arg, *this);
                return ScalarValue(resolved_str);
            }
        }

        // Check for default fallback syntax: "${path.to.key:default_val}"
        std::string path = expr;
        std::string default_value{};
        bool has_default = false;
        if (colon_pos != std::string::npos && colon_pos > 0) {
            path = expr.substr(0, colon_pos);
            default_value = expr.substr(colon_pos + 1);
            has_default = true;
            while (!path.empty() && std::isspace(static_cast<unsigned char>(path.front()))) path.erase(path.begin());
            while (!path.empty() && std::isspace(static_cast<unsigned char>(path.back()))) path.pop_back();
            while (!default_value.empty() && std::isspace(static_cast<unsigned char>(default_value.front()))) default_value.erase(default_value.begin());
            while (!default_value.empty() && std::isspace(static_cast<unsigned char>(default_value.back()))) default_value.pop_back();
        }

        // Relative dot navigation syntax (.sibling or ..parent)
        std::string absolute_path = resolve_absolute_path(path, current_path);

        auto resolved_node = root_ ? root_->select_node(absolute_path) : nullptr;
        if (!resolved_node) {
            if (has_default) {
                return ScalarValue(default_value);
            }
            throw ConfigInterpolationException("Cannot resolve reference '${" + expr + "}': path '" + absolute_path + "' does not exist.", source);
        }

        if (resolved_node->is_value()) {
            auto* val = resolved_node->as_value();
            if (val->is_missing_marker()) {
                throw MissingValueMarkerException(absolute_path, val->source());
            }

            if (val->is_string()) {
                const auto& s = val->as_string();
                if (s.find("${") != std::string::npos) {
                    return resolve_string_value(s, absolute_path, val->source());
                }
            }
            return val->value();
        }

        return resolved_node;
    }

    static std::string format_resolved(const ResolvedValue& res) {
        if (std::holds_alternative<std::shared_ptr<ConfigNode>>(res)) {
            auto node = std::get<std::shared_ptr<ConfigNode>>(res);
            return node ? node->to_string() : "null";
        }
        const auto& val = std::get<ScalarValue>(res);
        return std::visit([](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) return "null";
            if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
            if constexpr (std::is_same_v<T, int64_t>) return std::to_string(v);
            if constexpr (std::is_same_v<T, double>) {
                std::ostringstream ss;
                ss << v;
                return ss.str();
            }
            if constexpr (std::is_same_v<T, std::string>) return v;
        }, val);
    }

    static bool is_single_outer_interpolation(std::string_view raw, std::string& out_inner) {
        out_inner.clear();
        if (raw.size() < 3 || !starts_with(raw, "${") || !ends_with(raw, '}')) {
            return false;
        }

        int depth = 0;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 2 < raw.size() && raw[i + 1] == '$' && raw[i + 2] == '{') {
                i += 2;
                continue;
            }

            if (raw[i] == '$' && i + 1 < raw.size() && raw[i + 1] == '{') {
                depth++;
                i++;
                continue;
            }

            if (raw[i] == '{') {
                depth++;
            } else if (raw[i] == '}') {
                depth--;
                if (depth == 0 && i < raw.size() - 1) {
                    return false;
                }
            }
        }

        if (depth == 0) {
            out_inner = std::string(raw.substr(2, raw.size() - 3));
            while (!out_inner.empty() && std::isspace(static_cast<unsigned char>(out_inner.front()))) out_inner.erase(out_inner.begin());
            while (!out_inner.empty() && std::isspace(static_cast<unsigned char>(out_inner.back()))) out_inner.pop_back();
            return true;
        }

        return false;
    }

    static std::string resolve_absolute_path(std::string_view path, std::string_view current_path) {
        if (path.empty() || !starts_with(path, '.')) {
            return std::string(path);
        }

        size_t dot_count = 0;
        while (dot_count < path.size() && path[dot_count] == '.') {
            dot_count++;
        }

        std::string_view relative_key = path.substr(dot_count);
        while (!relative_key.empty() && relative_key.front() == '.') {
            relative_key.remove_prefix(1);
        }

        auto segments = split_path_segments(current_path);
        if (!segments.empty()) {
            segments.pop_back(); // Remove leaf segment
        }

        // 1 dot (.key) stays in current container; each additional dot ascends one parent container
        size_t levels_up = dot_count - 1;
        while (levels_up > 0 && !segments.empty()) {
            segments.pop_back();
            levels_up--;
        }

        if (relative_key.empty()) {
            return join_path_segments(segments);
        }

        if (segments.empty()) {
            return std::string(relative_key);
        }

        return join_path_segments(segments) + "." + std::string(relative_key);
    }

    static std::vector<std::string> split_path_segments(std::string_view current_path) {
        std::vector<std::string> result;
        if (current_path.empty()) return result;

        size_t start = 0;
        while (start < current_path.size()) {
            size_t dot_pos = current_path.find('.', start);
            std::string_view dot_part = (dot_pos == std::string_view::npos)
                ? current_path.substr(start)
                : current_path.substr(start, dot_pos - start);
            start = (dot_pos == std::string_view::npos) ? current_path.size() : dot_pos + 1;

            if (dot_part.empty()) continue;

            size_t bracket_idx = dot_part.find('[');
            if (bracket_idx == std::string_view::npos) {
                result.emplace_back(dot_part);
                continue;
            }

            if (bracket_idx > 0) {
                result.emplace_back(dot_part.substr(0, bracket_idx));
            }

            std::string_view rest = dot_part.substr(bracket_idx);
            while (starts_with(rest, '[')) {
                size_t close_idx = rest.find(']');
                if (close_idx == std::string_view::npos) break;
                result.emplace_back(rest.substr(0, close_idx + 1));
                rest.remove_prefix(close_idx + 1);
            }
        }

        return result;
    }

    static std::string join_path_segments(const std::vector<std::string>& segments) {
        if (segments.empty()) return "";
        std::string res = segments[0];
        for (size_t i = 1; i < segments.size(); ++i) {
            if (starts_with(segments[i], '[')) {
                res += segments[i];
            } else {
                res += "." + segments[i];
            }
        }
        return res;
    }

    static void replace_all(std::string& str, std::string_view from, std::string_view to) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
};

} // namespace omegaconf
