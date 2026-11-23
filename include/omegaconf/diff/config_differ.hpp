#pragma once

#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include "omegaconf/ast/config_list_node.hpp"
#include "omegaconf/ast/config_object_node.hpp"

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <algorithm>
#include <set>
#include <optional>

namespace omegaconf {

enum class ConfigDiffChangeType {
    Added,
    Removed,
    Modified
};

struct ConfigDiffEntry {
    std::string path{};
    ConfigDiffChangeType change_type{ConfigDiffChangeType::Modified};
    std::string old_value{};
    std::string new_value{};

    [[nodiscard]] std::string to_string() const {
        std::ostringstream ss;
        switch (change_type) {
            case ConfigDiffChangeType::Added:
                ss << "+ " << path << ": " << new_value;
                break;
            case ConfigDiffChangeType::Removed:
                ss << "- " << path << ": " << old_value;
                break;
            case ConfigDiffChangeType::Modified:
                ss << "~ " << path << ": " << old_value << " -> " << new_value;
                break;
        }
        return ss.str();
    }
};

class ConfigDiff {
public:
    explicit ConfigDiff(std::vector<ConfigDiffEntry> entries)
        : entries_(std::move(entries)) {}

    [[nodiscard]] const std::vector<ConfigDiffEntry>& entries() const noexcept {
        return entries_;
    }

    [[nodiscard]] bool has_changes() const noexcept {
        return !entries_.empty();
    }

    [[nodiscard]] std::string to_string() const {
        if (!has_changes()) {
            return "No configuration differences.";
        }
        std::ostringstream ss;
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (i > 0) ss << "\n";
            ss << entries_[i].to_string();
        }
        return ss.str();
    }

private:
    std::vector<ConfigDiffEntry> entries_{};
};

class ConfigDiffer {
public:
    static ConfigDiff compare(const std::shared_ptr<ConfigNode>& base_node,
                              const std::shared_ptr<ConfigNode>& target_node) {
        return compare(base_node.get(), target_node.get());
    }

    static ConfigDiff compare(const ConfigNode* base_node, const ConfigNode* target_node) {
        std::vector<ConfigDiffEntry> entries;
        compare_nodes(base_node, target_node, "", entries);
        return ConfigDiff(std::move(entries));
    }

private:
    static std::string format_node_value(const ConfigNode* node) {
        if (!node) return "null";
        if (node->is_value()) {
            const auto* vn = node->as_value();
            if (vn->is_missing_marker()) return "???";
            if (vn->is_null()) return "null";
            if (vn->is_bool()) return vn->as_bool() ? "True" : "False";
            if (vn->is_integer()) return std::to_string(vn->as_integer());
            if (vn->is_double()) {
                std::ostringstream ss;
                ss << vn->as_double();
                return ss.str();
            }
            if (vn->is_string()) return "\"" + vn->as_string() + "\"";
            return "null";
        }
        return node->to_string();
    }

    struct CaseInsensitiveLess {
        bool operator()(const std::string& a, const std::string& b) const noexcept {
            auto it1 = a.begin();
            auto it2 = b.begin();
            for (; it1 != a.end() && it2 != b.end(); ++it1, ++it2) {
                unsigned char c1 = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(*it1)));
                unsigned char c2 = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(*it2)));
                if (c1 != c2) return c1 < c2;
            }
            return (it1 == a.end()) && (it2 != b.end());
        }
    };

    static void compare_nodes(const ConfigNode* a, const ConfigNode* b,
                              const std::string& current_path,
                              std::vector<ConfigDiffEntry>& entries) {
        // Case 1: One side is null
        if (!a && b) {
            entries.push_back({current_path, ConfigDiffChangeType::Added, "", format_node_value(b)});
            return;
        }
        if (a && !b) {
            entries.push_back({current_path, ConfigDiffChangeType::Removed, format_node_value(a), ""});
            return;
        }
        if (!a && !b) return;

        // Case 2: Both are object mappings
        if (a->is_object() && b->is_object()) {
            const auto* obj_a = a->as_object();
            const auto* obj_b = b->as_object();

            std::set<std::string, CaseInsensitiveLess> all_keys;
            for (const auto& k : obj_a->keys()) all_keys.insert(k);
            for (const auto& k : obj_b->keys()) all_keys.insert(k);

            for (const auto& key : all_keys) {
                std::string child_path = current_path.empty() ? key : current_path + "." + key;
                auto child_a = obj_a->get_child(key);
                auto child_b = obj_b->get_child(key);

                if (!child_a && child_b) {
                    entries.push_back({child_path, ConfigDiffChangeType::Added, "", format_node_value(child_b.get())});
                } else if (child_a && !child_b) {
                    entries.push_back({child_path, ConfigDiffChangeType::Removed, format_node_value(child_a.get()), ""});
                } else {
                    compare_nodes(child_a.get(), child_b.get(), child_path, entries);
                }
            }
            return;
        }

        // Case 3: Both are lists
        if (a->is_list() && b->is_list()) {
            std::string str_a = a->to_string();
            std::string str_b = b->to_string();
            if (str_a != str_b) {
                entries.push_back({current_path, ConfigDiffChangeType::Modified, str_a, str_b});
            }
            return;
        }

        // Case 4: Scalar values or differing node types
        std::string val_a = format_node_value(a);
        std::string val_b = format_node_value(b);
        if (val_a != val_b) {
            entries.push_back({current_path, ConfigDiffChangeType::Modified, val_a, val_b});
        }
    }
};

} // namespace omegaconf
