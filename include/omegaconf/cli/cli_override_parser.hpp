#pragma once

#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include "omegaconf/ast/config_list_node.hpp"
#include "omegaconf/ast/config_object_node.hpp"
#include "omegaconf/operators/operator_parser.hpp"
#include "omegaconf/operators/operator_engine.hpp"
#include "omegaconf/parsers/yaml_format_parser.hpp"
#include "omegaconf/multirun/sweep_engine.hpp"
#include "omegaconf/common/exceptions.hpp"

#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <algorithm>
#include <cctype>

namespace omegaconf {

class CliOverrideParser {
public:
    static void apply_overrides(
        const std::shared_ptr<ConfigObjectNode>& root,
        const std::vector<std::string>& cli_args,
        bool struct_mode = false) {

        if (!root || cli_args.empty()) return;

        for (const auto& arg : cli_args) {
            std::string_view trimmed = trim(arg);
            if (trimmed.empty()) continue;

            // Handle deletion: ~path or ~path=expected
            if (starts_with(trimmed, '~')) {
                std::string target(trimmed.substr(1));
                size_t eq_idx = target.find('=');
                std::string path = eq_idx == std::string::npos ? target : target.substr(0, eq_idx);
                std::optional<std::string> expected = eq_idx == std::string::npos ? std::nullopt : std::optional<std::string>(target.substr(eq_idx + 1));

                while (!path.empty() && std::isspace(static_cast<unsigned char>(path.front()))) path.erase(path.begin());
                while (!path.empty() && std::isspace(static_cast<unsigned char>(path.back()))) path.pop_back();

                bool removed = remove_deep_property(root, path, expected);
                if (!removed) {
                    if (expected.has_value() || struct_mode || root->is_struct()) {
                        throw ConfigStructException(
                            path,
                            expected.has_value()
                                ? ("Could not delete '~" + path + "=" + *expected + "': key does not exist or value mismatch.")
                                : ("Cannot delete '~" + path + "': key does not exist in struct-locked configuration."),
                            SourceInfo{"(cli)", 0, 0});
                    }
                }
                continue;
            }

            auto [raw_path, op, val] = OperatorParser::parse_assignment(trimmed);

            bool is_force_upsert = starts_with(raw_path, "++");
            bool is_strict_append = !is_force_upsert && starts_with(raw_path, '+');
            std::string clean_path = raw_path;
            while (!clean_path.empty() && clean_path.front() == '+') clean_path.erase(clean_path.begin());

            if (is_strict_append && starts_with(val, "extend_list(")) {
                throw ConfigSyntaxException("Cannot combine append prefix '+' with 'extend_list(...)' in '" + std::string(trimmed) + "'.", SourceInfo{"(cli)", 0, 0});
            }

            auto override_node = parse_override_value(val, op);
            set_deep_property(root, clean_path, override_node, is_strict_append, is_force_upsert, struct_mode);
        }
    }

private:
    struct PathStep {
        std::string key{};
        bool is_bracket_index{false};
    };

    static std::string_view trim(std::string_view s) noexcept {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    }

    static std::vector<PathStep> parse_path_steps(std::string_view path) {
        std::vector<PathStep> steps;
        size_t start = 0;
        while (start < path.size()) {
            size_t dot = path.find('.', start);
            std::string_view part = dot == std::string_view::npos ? path.substr(start) : path.substr(start, dot - start);

            size_t bracket = part.find('[');
            if (bracket == std::string_view::npos) {
                if (!part.empty()) steps.push_back({std::string(part), false});
            } else {
                if (bracket > 0) {
                    steps.push_back({std::string(part.substr(0, bracket)), false});
                }
                std::string_view rest = part.substr(bracket);
                while (starts_with(rest, '[')) {
                    size_t close = rest.find(']');
                    if (close == std::string_view::npos) break;
                    steps.push_back({std::string(trim(rest.substr(1, close - 1))), true});
                    rest = rest.substr(close + 1);
                }
            }

            if (dot == std::string_view::npos) break;
            start = dot + 1;
        }
        return steps;
    }

    static ConfigNode* step_into(ConfigNode* current, const PathStep& step) {
        if (!current) return nullptr;
        if (current->is_object()) {
            auto* obj = current->as_object();
            auto child = obj->get_child(step.key);
            return child ? child.get() : nullptr;
        }
        if (current->is_list()) {
            try {
                auto* lst = current->as_list();
                int idx = std::stoi(step.key);
                int actual = idx < 0 ? static_cast<int>(lst->size()) + idx : idx;
                if (actual >= 0 && actual < static_cast<int>(lst->size())) {
                    return (*lst)[static_cast<size_t>(actual)].get();
                }
            } catch (...) {}
        }
        return nullptr;
    }

    static bool remove_deep_property(const std::shared_ptr<ConfigObjectNode>& root,
                                     const std::string& path,
                                     const std::optional<std::string>& expected_raw_value) {
        auto steps = parse_path_steps(path);
        if (steps.empty()) return false;

        ConfigNode* current = root.get();
        for (size_t i = 0; i + 1 < steps.size(); ++i) {
            current = step_into(current, steps[i]);
            if (!current) return false;
        }

        const auto& last_step = steps.back();
        if (current->is_list()) {
            try {
                auto* lst = current->as_list();
                int idx = std::stoi(last_step.key);
                int actual = idx < 0 ? static_cast<int>(lst->size()) + idx : idx;
                if (actual >= 0 && actual < static_cast<int>(lst->size())) {
                    if (expected_raw_value.has_value() &&
                        !node_matches_expected_cli_value((*lst)[static_cast<size_t>(actual)].get(), *expected_raw_value)) {
                        return false;
                    }
                    lst->remove_at(static_cast<size_t>(actual));
                    return true;
                }
            } catch (...) {}
            return false;
        }

        if (current->is_object()) {
            auto* obj = current->as_object();
            auto child = obj->get_child(last_step.key);
            if (!child) return false;

            if (expected_raw_value.has_value() &&
                !node_matches_expected_cli_value(child.get(), *expected_raw_value)) {
                return false;
            }

            return obj->remove_child(last_step.key);
        }

        return false;
    }

    static bool node_matches_expected_cli_value(const ConfigNode* node, const std::string& expected_raw) {
        if (!node) return expected_raw == "null";

        auto exp_node = parse_override_value(expected_raw, ConfigOperator::Assign);
        if (node->is_value() && exp_node->is_value()) {
            const auto* vn = node->as_value();
            const auto* evn = exp_node->as_value();
            if (vn->is_missing_marker() != evn->is_missing_marker()) {
                return false;
            }
            if (vn->is_missing_marker() && evn->is_missing_marker()) {
                return true;
            }
            if (vn->is_null() && evn->is_null()) {
                return true;
            }
            return vn->to_string() == evn->to_string();
        }

        return node->to_string() == exp_node->to_string();
    }

    static void set_deep_property(
        const std::shared_ptr<ConfigObjectNode>& root,
        const std::string& path,
        const std::shared_ptr<ConfigNode>& override_node,
        bool is_strict_append,
        bool is_force_upsert,
        bool struct_mode) {

        auto steps = parse_path_steps(path);
        if (steps.empty()) return;

        ConfigNode* current = root.get();

        for (size_t i = 0; i + 1 < steps.size(); ++i) {
            const auto& step = steps[i];
            auto* next = step_into(current, step);
            if (!next) {
                if (current->is_object()) {
                    auto* curr_obj = current->as_object();
                    if ((struct_mode || curr_obj->is_struct()) && !is_strict_append && !is_force_upsert) {
                        throw ConfigStructException(
                            path,
                            "Cannot set '" + path + "': parent container '" + step.key + "' does not exist in struct-locked configuration.",
                            SourceInfo{"(cli)", 0, 0});
                    }
                    auto new_obj = std::make_shared<ConfigObjectNode>();
                    curr_obj->set_child(step.key, new_obj, true);
                    current = new_obj.get();
                    continue;
                }
                return;
            }
            current = next;
        }

        const auto& last_step = steps.back();
        if (current->is_object()) {
            auto* curr_obj = current->as_object();
            bool effective_struct = struct_mode || curr_obj->is_struct();
            auto existing = curr_obj->get_child(last_step.key);

            if (is_strict_append && existing && effective_struct) {
                throw ConfigStructException(
                    path,
                    "Cannot add '+" + path + "': key already exists in configuration.",
                    SourceInfo{"(cli)", 0, 0});
            }

            if (!existing && effective_struct && !is_strict_append && !is_force_upsert) {
                throw ConfigStructException(
                    path,
                    "Cannot set '" + path + "': key does not exist in struct-locked configuration.",
                    SourceInfo{"(cli)", 0, 0});
            }

            if (existing) {
                curr_obj->set_child(last_step.key, OperatorEngine::apply(existing, override_node), true);
            } else {
                curr_obj->set_child(last_step.key, override_node, true);
            }
        } else if (current->is_list()) {
            try {
                auto* lst = current->as_list();
                int idx = std::stoi(last_step.key);
                int actual = idx < 0 ? static_cast<int>(lst->size()) + idx : idx;
                if (actual >= 0 && actual < static_cast<int>(lst->size())) {
                    auto existing = (*lst)[static_cast<size_t>(actual)];
                    (*lst)[static_cast<size_t>(actual)] = OperatorEngine::apply(existing, override_node);
                }
            } catch (...) {}
        }
    }

    static std::shared_ptr<ConfigNode> parse_override_value(std::string_view val, ConfigOperator op) {
        val = trim(val);
        SourceInfo src{"(cli)", 0, 0};

        if (ends_with(val, ')')) {
            size_t open_paren = val.find('(');
            if (open_paren != std::string_view::npos && open_paren > 0) {
                std::string func_name = std::string(trim(val.substr(0, open_paren)));
                to_lower_in_place(func_name);
                std::string inner = std::string(trim(val.substr(open_paren + 1, val.size() - open_paren - 2)));

                if (func_name == "extend_list") {
                    auto args = SweepEngine::split_top_level_commas(inner);
                    auto list_node = std::make_shared<ConfigListNode>(ConfigOperator::Add, src);
                    for (const auto& item : args) {
                        list_node->add_item(parse_override_value(item, ConfigOperator::Assign));
                    }
                    return list_node;
                }

                if (func_name == "sort") {
                    auto args = SweepEngine::split_top_level_commas(inner);
                    bool reverse = false;
                    if (!args.empty()) {
                        std::string last = args.back();
                        last.erase(std::remove_if(last.begin(), last.end(), ::isspace), last.end());
                        to_lower_in_place(last);
                        if (last == "reverse=true") {
                            reverse = true;
                            args.pop_back();
                        } else if (last == "reverse=false") {
                            args.pop_back();
                        }
                    }
                    if (args.size() == 1 && starts_with(args[0], '[') && ends_with(args[0], ']')) {
                        args = SweepEngine::split_top_level_commas(args[0].substr(1, args[0].size() - 2));
                    }
                    bool all_num = true;
                    for (const auto& a : args) {
                        try {
                            size_t idx = 0;
                            (void)std::stod(a, &idx);
                            if (idx != a.size()) all_num = false;
                        } catch (...) { all_num = false; break; }
                    }
                    if (all_num) {
                        std::sort(args.begin(), args.end(), [](const std::string& a, const std::string& b) {
                            return std::stod(a) < std::stod(b);
                        });
                    } else {
                        std::sort(args.begin(), args.end());
                    }
                    if (reverse) std::reverse(args.begin(), args.end());
                    auto list_node = std::make_shared<ConfigListNode>(op, src);
                    for (const auto& a : args) {
                        list_node->add_item(parse_override_value(a, ConfigOperator::Assign));
                    }
                    return list_node;
                }

                if (func_name == "str") {
                    if (inner.size() >= 2 && ((inner.front() == '"' && inner.back() == '"') || (inner.front() == '\'' && inner.back() == '\''))) {
                        inner = inner.substr(1, inner.size() - 2);
                    }
                    return std::make_shared<ConfigValueNode>(inner, op, src);
                }

                if (func_name == "int") {
                    if (inner.size() >= 2 && ((inner.front() == '"' && inner.back() == '"') || (inner.front() == '\'' && inner.back() == '\''))) {
                        inner = inner.substr(1, inner.size() - 2);
                    }
                    return std::make_shared<ConfigValueNode>(std::stoll(inner), op, src);
                }

                if (func_name == "float") {
                    if (inner.size() >= 2 && ((inner.front() == '"' && inner.back() == '"') || (inner.front() == '\'' && inner.back() == '\''))) {
                        inner = inner.substr(1, inner.size() - 2);
                    }
                    return std::make_shared<ConfigValueNode>(std::stod(inner), op, src);
                }

                if (func_name == "bool") {
                    if (inner.size() >= 2 && ((inner.front() == '"' && inner.back() == '"') || (inner.front() == '\'' && inner.back() == '\''))) {
                        inner = inner.substr(1, inner.size() - 2);
                    }
                    std::string l = inner;
                    to_lower_in_place(l);
                    bool b = (l == "true" || l == "1" || l == "yes" || l == "on");
                    return std::make_shared<ConfigValueNode>(b, op, src);
                }
            }
        }

        if (val == ConfigValueNode::MissingMarker) {
            return std::make_shared<ConfigValueNode>(std::string(ConfigValueNode::MissingMarker), op, src);
        }

        if (starts_with(val, '\\') && val.substr(1) == ConfigValueNode::MissingMarker) {
            auto node = std::make_shared<ConfigValueNode>(std::string(val.substr(1)), op, src);
            node->set_escaped_literal(true);
            return node;
        }

        if (val == "null" || val == "~") {
            return std::make_shared<ConfigValueNode>(nullptr, op, src);
        }

        if (val == "true" || val == "True") return std::make_shared<ConfigValueNode>(true, op, src);
        if (val == "false" || val == "False") return std::make_shared<ConfigValueNode>(false, op, src);

        // Try integer
        try {
            size_t idx = 0;
            int64_t int_val = std::stoll(std::string(val), &idx, 0);
            if (idx == val.size()) {
                return std::make_shared<ConfigValueNode>(int_val, op, src);
            }
        } catch (...) {}

        // Try double
        if (val.find('.') != std::string_view::npos ||
            val.find('e') != std::string_view::npos ||
            val.find('E') != std::string_view::npos) {
            try {
                size_t idx = 0;
                double dbl_val = std::stod(std::string(val), &idx);
                if (idx == val.size()) {
                    return std::make_shared<ConfigValueNode>(dbl_val, op, src);
                }
            } catch (...) {}
        }

        // List or object syntax
        if ((starts_with(val, '[') && ends_with(val, ']')) ||
            (starts_with(val, '{') && ends_with(val, '}'))) {
            try {
                auto parsed = YamlFormatParser::parse("_val_: " + std::string(val));
                auto child = parsed->get_child("_val_");
                if (child) {
                    child->set_op(op);
                    return child;
                }
            } catch (...) {}
        }

        // Quoted string
        if (val.size() >= 2 &&
            ((starts_with(val, '"') && ends_with(val, '"')) ||
             (starts_with(val, '\'') && ends_with(val, '\'')))) {
            return std::make_shared<ConfigValueNode>(std::string(val.substr(1, val.size() - 2)), op, src);
        }

        return std::make_shared<ConfigValueNode>(std::string(val), op, src);
    }
};

} // namespace omegaconf
