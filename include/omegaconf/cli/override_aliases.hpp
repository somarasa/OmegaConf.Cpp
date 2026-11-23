#pragma once

#include "omegaconf/ast/config_object_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include "omegaconf/common/exceptions.hpp"

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace omegaconf {

class OverrideAliases {
public:
    OverrideAliases() = default;

    explicit OverrideAliases(std::map<std::string, std::string> targets)
        : targets_(std::move(targets)) {}

    [[nodiscard]] const std::map<std::string, std::string>& targets() const noexcept {
        return targets_;
    }

    [[nodiscard]] bool is_empty() const noexcept {
        return targets_.empty();
    }

    static OverrideAliases from_config(
        const std::shared_ptr<ConfigObjectNode>& primary_config,
        const std::map<std::string, std::string>& builder_aliases = {}) {
        std::map<std::string, std::string> targets;

        if (primary_config) {
            std::vector<std::string> root_keys = {"hydra", "omegaconf"};
            for (const auto& root_key : root_keys) {
                auto sec_node = primary_config->get_child(root_key);
                if (!sec_node || !sec_node->is_object()) continue;

                auto aliases_node = sec_node->as_object()->get_child("aliases");
                if (!aliases_node) continue;

                if (!aliases_node->is_object()) {
                    throw ConfigSyntaxException("'" + root_key + ".aliases' must be a mapping of alias names to target overrides.",
                                                aliases_node->source());
                }

                auto* aliases_obj = aliases_node->as_object();
                for (const auto& raw_names : aliases_obj->keys()) {
                    auto target_node = aliases_obj->get_child(raw_names);
                    if (!target_node || !target_node->is_value()) {
                        throw ConfigSyntaxException("Invalid target for alias '" + raw_names + "' in '" + root_key + ".aliases'.");
                    }
                    auto* tvn = target_node->as_value();
                    if (!tvn->is_string() || tvn->is_missing_marker() || contains_unescaped_interpolation(tvn->as_string())) {
                        throw ConfigSyntaxException("Invalid target for alias '" + raw_names + "' in '" + root_key + ".aliases': target must be a non-empty literal string without interpolations.",
                                                    target_node->source());
                    }
                    register_alias_entry(targets, raw_names, tvn->as_string(), target_node->source());
                }
            }
        }

        for (const auto& [raw_names, raw_target] : builder_aliases) {
            register_alias_entry(targets, raw_names, raw_target, SourceInfo::unknown());
        }

        return OverrideAliases(std::move(targets));
    }

    static void validate_no_aliases_in_included_config(const ConfigObjectNode* node, const std::string& file_path) {
        if (!node) return;
        std::vector<std::string> root_keys = {"hydra", "omegaconf"};
        for (const auto& root_key : root_keys) {
            auto sec_node = node->get_child(root_key);
            if (sec_node && sec_node->is_object()) {
                auto aliases_node = sec_node->as_object()->get_child("aliases");
                if (aliases_node) {
                    throw ConfigSyntaxException("'" + root_key + ".aliases' can only be defined in the primary configuration file, not in included config '" + file_path + "'.",
                                                aliases_node->source());
                }
            }
        }
    }

    static void validate_cli_does_not_modify_aliases(const std::vector<std::string>& cli_args) {
        for (const auto& arg : cli_args) {
            if (arg.empty()) continue;
            auto [prefix, key, value] = split_override(arg);
            std::string lk = key;
            to_lower_in_place(lk);
            if (lk == "hydra.aliases" || starts_with(lk, "hydra.aliases.") ||
                lk == "omegaconf.aliases" || starts_with(lk, "omegaconf.aliases.")) {
                throw ConfigSyntaxException("hydra.aliases cannot be modified from the command line.");
            }
        }
    }

    [[nodiscard]] std::string expand(const std::string& override_arg) const {
        if (is_empty() || override_arg.empty()) {
            return override_arg;
        }

        std::string trimmed = override_arg;
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();

        auto [prefix, key, value] = split_override(trimmed);

        auto it = targets_.find(key);
        if (it == targets_.end()) {
            return override_arg;
        }

        const std::string& target = it->second;
        auto [target_prefix, target_key, target_value] = split_override(target);

        // Value alias
        if (!target_value.empty()) {
            if (!value.empty()) {
                return override_arg;
            }
            std::string eff_prefix = !prefix.empty() ? prefix : target_prefix;
            return eff_prefix + target_key + target_value;
        }

        // Key alias
        if (value.empty() && prefix != "~") {
            throw ConfigSyntaxException("Override alias '" + key + "' requires a value for '" + trimmed + "'.");
        }

        return prefix + target + value;
    }

    [[nodiscard]] std::string format(const std::vector<std::string>& cli_args = {}) const {
        std::ostringstream ss;
        ss << "== Override aliases ==\n\n";

        struct GroupedAlias {
            std::vector<std::string> names;
            std::string target;
        };
        std::vector<GroupedAlias> grouped;

        for (const auto& [name, target] : targets_) {
            auto it = std::find_if(grouped.begin(), grouped.end(), [&](const GroupedAlias& g) {
                return g.target == target;
            });
            if (it != grouped.end()) {
                it->names.push_back(name);
            } else {
                grouped.push_back({{name}, target});
            }
        }

        for (const auto& g : grouped) {
            for (size_t i = 0; i < g.names.size(); ++i) {
                if (i > 0) ss << ", ";
                ss << g.names[i];
            }
            ss << " -> " << g.target << "\n";
        }

        if (!cli_args.empty()) {
            std::vector<std::pair<std::string, std::string>> expansions;
            for (const auto& arg : cli_args) {
                try {
                    std::string exp = expand(arg);
                    if (exp != arg) {
                        expansions.push_back({arg, exp});
                    }
                } catch (...) {
                    auto parts = split_override(arg);
                    auto it = targets_.find(parts.key);
                    if (it != targets_.end()) {
                        expansions.push_back({arg, parts.prefix + it->second + parts.value});
                    }
                }
            }

            if (!expansions.empty()) {
                ss << "\nExpanded overrides:\n";
                for (const auto& [orig, exp] : expansions) {
                    ss << "  " << orig << " -> " << exp << "\n";
                }
            }
        }

        std::string res = ss.str();
        while (!res.empty() && std::isspace(static_cast<unsigned char>(res.back()))) res.pop_back();
        return res;
    }

    struct SplitParts {
        std::string prefix{};
        std::string key{};
        std::string value{};
    };

    static SplitParts split_override(const std::string& raw) {
        std::string prefix = "";
        std::string rest = raw;

        if (starts_with(rest, "++")) {
            prefix = "++";
            rest = rest.substr(2);
        } else if (starts_with(rest, "+")) {
            prefix = "+";
            rest = rest.substr(1);
        } else if (starts_with(rest, "~")) {
            prefix = "~";
            rest = rest.substr(1);
        }

        size_t eq_idx = rest.find('=');
        if (eq_idx == std::string::npos) {
            return {prefix, rest, ""};
        }

        if (eq_idx > 0) {
            char op_char = rest[eq_idx - 1];
            if (op_char == '+' || op_char == '-' || op_char == '*' || op_char == '/' ||
                op_char == '&' || op_char == '|' || op_char == '!') {
                return {prefix, rest.substr(0, eq_idx - 1), rest.substr(eq_idx - 1)};
            }
        }

        return {prefix, rest.substr(0, eq_idx), rest.substr(eq_idx)};
    }

private:
    std::map<std::string, std::string> targets_{};

    static bool contains_unescaped_interpolation(std::string_view str) {
        for (size_t i = 0; i + 1 < str.size(); ++i) {
            if (str[i] == '$' && str[i + 1] == '{' && (i == 0 || str[i - 1] != '\\')) {
                return true;
            }
        }
        return false;
    }

    static void register_alias_entry(
        std::map<std::string, std::string>& targets,
        const std::string& raw_names,
        const std::string& raw_target,
        const SourceInfo& source) {
        if (raw_names.empty() || contains_unescaped_interpolation(raw_names)) {
            throw ConfigSyntaxException("Invalid alias name '" + raw_names + "': alias names must be non-empty literal keys.", source);
        }
        if (raw_target.empty() || contains_unescaped_interpolation(raw_target)) {
            throw ConfigSyntaxException("Invalid target '" + raw_target + "' for alias '" + raw_names + "': target must be a non-empty literal string.", source);
        }

        auto [target_prefix, target_key, target_value] = split_override(raw_target);
        if (target_key.empty()) {
            throw ConfigSyntaxException("Invalid target '" + raw_target + "' for alias '" + raw_names + "'.", source);
        }

        if (target_value.empty() && !target_prefix.empty()) {
            throw ConfigSyntaxException("Invalid target '" + raw_target + "' for alias '" + raw_names + "': key-only alias targets cannot include a prefix.", source);
        }

        std::stringstream ss(raw_names);
        std::string part;
        while (std::getline(ss, part, ',')) {
            while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front()))) part.erase(part.begin());
            while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back()))) part.pop_back();
            if (part.empty()) {
                throw ConfigSyntaxException("Empty alias name in '" + raw_names + "'.", source);
            }

            auto [np, nk, nv] = split_override(part);
            if (!np.empty() || !nv.empty() || nk.empty()) {
                throw ConfigSyntaxException("Invalid alias name '" + part + "': alias names cannot contain prefixes ('+', '++', '~') or '=value' assignments.", source);
            }

            if (targets.find(part) != targets.end()) {
                throw ConfigSyntaxException("Duplicate override alias '" + part + "'.", source);
            }

            targets[part] = raw_target;
        }
    }
};

} // namespace omegaconf
