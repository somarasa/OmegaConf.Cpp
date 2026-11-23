#pragma once

#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include "omegaconf/ast/config_list_node.hpp"
#include "omegaconf/ast/config_object_node.hpp"
#include "omegaconf/composition/defaults_entry.hpp"
#include "omegaconf/common/exceptions.hpp"

#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>

namespace omegaconf {

class DefaultsListParser {
public:
    static constexpr std::string_view DefaultsKey = "defaults";

    static std::vector<DefaultsEntry> parse(const ConfigObjectNode* node) {
        std::vector<DefaultsEntry> result;
        if (!node) return result;

        auto defaults_node = node->get_child(DefaultsKey);
        if (!defaults_node || !defaults_node->is_list()) {
            return result;
        }

        const auto* list = defaults_node->as_list();
        bool has_self = false;

        for (size_t i = 0; i < list->size(); ++i) {
            const auto& item = (*list)[i];
            if (!item) continue;

            if (item->is_value()) {
                const auto* vn = item->as_value();
                std::string s = vn->as_string();
                std::string_view trimmed = trim(s);

                if (trimmed == DefaultsEntry::SelfMarker) {
                    if (has_self) {
                        throw ConfigSyntaxException("Duplicate '_self_' in defaults list.", vn->source());
                    }
                    has_self = true;
                    result.push_back(DefaultsEntry::make_self());
                    continue;
                }

                // Bare single entry (e.g. - base_config, - /item@_group_.suffix, - db/postgres)
                result.push_back(parse_entry_from_string(trimmed, vn->source()));
            } else if (item->is_object()) {
                const auto* obj = item->as_object();
                const auto& keys = obj->keys();
                for (const auto& key : keys) {
                    auto child_val = obj->get_child(key);
                    result.push_back(parse_key_value(key, child_val.get(), obj->source()));
                }
            }
        }

        // Hydra rule: if _self_ is omitted, it defaults to the end
        if (!has_self && !result.empty()) {
            result.push_back(DefaultsEntry::make_self());
        }

        return result;
    }

    static void split_package(std::string_view raw_key, std::string& out_group, std::string& out_package) {
        out_group.clear();
        out_package.clear();

        size_t at_pos = raw_key.find('@');
        if (at_pos != std::string_view::npos) {
            out_group = std::string(trim(raw_key.substr(0, at_pos)));
            out_package = std::string(trim(raw_key.substr(at_pos + 1)));
        } else {
            out_group = std::string(trim(raw_key));
        }
    }

private:
    static DefaultsEntry parse_entry_from_string(std::string_view raw, const SourceInfo& source) {
        bool is_optional = false;
        bool is_override = false;
        std::string_view key = raw;

        while (true) {
            if (starts_with(key, "optional ")) {
                is_optional = true;
                key = trim(key.substr(9));
            } else if (starts_with(key, "override ")) {
                is_override = true;
                key = trim(key.substr(9));
            } else {
                break;
            }
        }

        if (is_optional && is_override) {
            throw ConfigSyntaxException("'optional' and 'override' keywords cannot be combined in defaults list entry '" + std::string(raw) + "'.", source);
        }

        bool is_rooted = starts_with(key, '/');
        while (starts_with(key, '/')) key.remove_prefix(1);

        std::string without_pkg, pkg;
        split_package(key, without_pkg, pkg);

        if (without_pkg.find("..") != std::string::npos) {
            throw ConfigSyntaxException("Parent directory traversal ('..') is not permitted in defaults list.", source);
        }

        size_t slash = without_pkg.rfind('/');
        if (slash != std::string::npos && slash > 0) {
            std::string grp = without_pkg.substr(0, slash);
            std::string choice = without_pkg.substr(slash + 1);
            return DefaultsEntry::make_group_choice(grp, choice, is_optional, pkg, is_override, false, false, {}, is_rooted, false);
        } else {
            return DefaultsEntry::make_group_choice(without_pkg, without_pkg, is_optional, pkg, is_override, false, false, {}, is_rooted, true);
        }
    }

    static DefaultsEntry parse_key_value(std::string_view raw_key, const ConfigNode* val_node, const SourceInfo& source) {
        bool is_optional = false;
        bool is_override = false;
        std::string_view key = raw_key;

        while (true) {
            if (starts_with(key, "optional ")) {
                is_optional = true;
                key = trim(key.substr(9));
            } else if (starts_with(key, "override ")) {
                is_override = true;
                key = trim(key.substr(9));
            } else {
                break;
            }
        }

        if (is_optional && is_override) {
            throw ConfigSyntaxException("'optional' and 'override' keywords cannot be combined in defaults list entry '" + std::string(raw_key) + "'.", source);
        }

        bool is_rooted = starts_with(key, '/');
        while (starts_with(key, '/')) key.remove_prefix(1);

        std::string group, pkg;
        split_package(key, group, pkg);

        if (group.find("..") != std::string::npos) {
            throw ConfigSyntaxException("Parent directory traversal ('..') is not permitted in defaults list.", source);
        }

        bool is_missing = false;
        bool is_disabled = false;
        std::string path_str{};
        std::vector<std::string> multi_paths{};

        if (val_node) {
            if (val_node->is_value()) {
                const auto* vn = val_node->as_value();
                if (vn->is_null() || vn->to_string() == "null" || vn->to_string().empty()) {
                    is_disabled = true;
                } else if (vn->is_missing_marker() || vn->to_string() == "???") {
                    is_missing = true;
                } else {
                    path_str = vn->as_string();
                }
            } else if (val_node->is_list()) {
                const auto* ln = val_node->as_list();
                for (size_t i = 0; i < ln->size(); ++i) {
                    if ((*ln)[i]) {
                        multi_paths.push_back((*ln)[i]->to_string());
                    }
                }
            }
        } else {
            is_disabled = true;
        }

        return DefaultsEntry::make_group_choice(group, path_str, is_optional, pkg, is_override, is_missing, is_disabled, multi_paths, is_rooted, false);
    }

    static std::string_view trim(std::string_view s) noexcept {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    }
};

} // namespace omegaconf
