#pragma once

#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include "omegaconf/ast/config_list_node.hpp"
#include "omegaconf/ast/config_object_node.hpp"
#include "omegaconf/parsers/yaml_format_parser.hpp"
#include "omegaconf/operators/operator_engine.hpp"
#include "omegaconf/composition/defaults_entry.hpp"
#include "omegaconf/composition/defaults_list_parser.hpp"
#include "omegaconf/composition/config_store.hpp"
#include "omegaconf/interpolation/interpolation_engine.hpp"
#include "omegaconf/cli/override_aliases.hpp"
#include "omegaconf/common/exceptions.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <optional>
#include <algorithm>

namespace fs = std::filesystem;

namespace omegaconf {

class TreeResolver {
public:
    TreeResolver() = default;

    std::shared_ptr<ConfigObjectNode> resolve(
        const std::string& root_dir,
        const std::string& entry_file = "config.yaml",
        const std::unordered_map<std::string, std::string>& selected_paths = {},
        const std::unordered_set<std::string>& dropped_groups = {},
        const std::vector<std::string>& search_paths = {}) {

        search_paths_ = { root_dir };
        for (const auto& sp : search_paths) {
            if (!sp.empty()) {
                search_paths_.push_back(sp);
            }
        }

        std::string full_entry = find_file_across_search_paths(entry_file);
        if (full_entry.empty()) {
            auto stored = ConfigStore::instance().try_load(entry_file);
            if (stored && stored->node()) {
                loading_stack_.clear();
                return load_node_hierarchy(root_dir, "store://" + stored->name(), stored->node(), selected_paths, dropped_groups, true);
            }
            throw ConfigInheritanceException("Entry configuration file '" + entry_file + "' not found in directory '" + root_dir + "' or ConfigStore.");
        }

        loading_stack_.clear();
        return load_file_hierarchy(root_dir, full_entry, selected_paths, dropped_groups, true);
    }

private:
    std::vector<std::string> search_paths_{};
    std::unordered_set<std::string> loading_stack_{};

    static std::string read_file_content(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw ConfigInheritanceException("Cannot open configuration file: " + path);
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    static std::optional<std::string> extract_file_package_header(const std::string& file_path) {
        std::ifstream file(file_path);
        if (!file.is_open()) return std::nullopt;
        std::string line;
        int count = 0;
        while (count++ < 15 && std::getline(file, line)) {
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
            if (line.empty()) continue;
            if (starts_with(line, "#") || starts_with(line, "//")) {
                std::string comment = line;
                while (!comment.empty() && (comment.front() == '#' || comment.front() == '/')) comment.erase(comment.begin());
                while (!comment.empty() && std::isspace(static_cast<unsigned char>(comment.front()))) comment.erase(comment.begin());
                if (starts_with(comment, "@package ") || starts_with(comment, "@package\t")) {
                    std::string pkg = comment.substr(9);
                    while (!pkg.empty() && std::isspace(static_cast<unsigned char>(pkg.front()))) pkg.erase(pkg.begin());
                    while (!pkg.empty() && std::isspace(static_cast<unsigned char>(pkg.back()))) pkg.pop_back();
                    if (!pkg.empty()) return pkg;
                }
            } else {
                break;
            }
        }
        return std::nullopt;
    }

    std::string find_file(const std::string& dir, const std::string& name) const {
        fs::path p = fs::path(dir) / name;
        if (fs::exists(p) && !fs::is_directory(p)) {
            return p.string();
        }

        static const std::vector<std::string> exts = { ".yaml", ".yml", ".json", ".jsonc", ".toml" };
        for (const auto& ext : exts) {
            fs::path candidate = fs::path(dir) / (name + ext);
            if (fs::exists(candidate) && !fs::is_directory(candidate)) {
                return candidate.string();
            }
        }
        return "";
    }

    std::string find_file_across_search_paths(const std::string& filename) const {
        if (fs::path(filename).is_absolute() && fs::exists(filename)) {
            return filename;
        }

        for (const auto& dir : search_paths_) {
            std::string found = find_file(dir, filename);
            if (!found.empty()) {
                return found;
            }
        }
        return "";
    }

    bool is_known_group(const std::string& group) const {
        if (ConfigStore::instance().has_group(group)) return true;
        for (const auto& dir : search_paths_) {
            fs::path p = fs::path(dir) / group;
            if (fs::exists(p) && fs::is_directory(p)) return true;
        }
        return false;
    }

    std::shared_ptr<ConfigObjectNode> load_file_hierarchy(
        const std::string& root_dir,
        const std::string& file_path,
        const std::unordered_map<std::string, std::string>& selected_paths,
        const std::unordered_set<std::string>& dropped_groups,
        bool is_entry) {

        std::string canonical = fs::canonical(file_path).string();
        if (loading_stack_.find(canonical) != loading_stack_.end()) {
            throw ConfigInheritanceException("Circular configuration dependency detected: '" + canonical + "'.", SourceInfo{canonical, 0, 0});
        }
        loading_stack_.insert(canonical);

        struct StackGuard {
            std::unordered_set<std::string>& s;
            std::string path;
            ~StackGuard() { s.erase(path); }
        } guard{loading_stack_, canonical};

        std::string content = read_file_content(canonical);
        auto raw_node = YamlFormatParser::parse(content, canonical);

        if (!is_entry) {
            OverrideAliases::validate_no_aliases_in_included_config(raw_node.get(), canonical);
        }

        return load_node_hierarchy(root_dir, canonical, raw_node, selected_paths, dropped_groups, is_entry);
    }

    std::shared_ptr<ConfigObjectNode> load_node_hierarchy(
        const std::string& root_dir,
        const std::string& canonical,
        const std::shared_ptr<ConfigObjectNode>& raw_node,
        const std::unordered_map<std::string, std::string>& selected_paths,
        const std::unordered_set<std::string>& dropped_groups,
        bool is_entry) {

        std::string current_dir = starts_with(canonical, "store://") ? root_dir : fs::path(canonical).parent_path().string();

        // Step 1: Explicit inheritance (_inherits_ or _base_)
        auto base_node = resolve_explicit_inheritance(root_dir, current_dir, raw_node, selected_paths, dropped_groups);

        std::shared_ptr<ConfigObjectNode> current_node = raw_node;
        if (base_node) {
            auto merged = OperatorEngine::apply(base_node, raw_node);
            current_node = std::static_pointer_cast<ConfigObjectNode>(merged);
        }

        // Step 2: Defaults list
        auto defaults = DefaultsListParser::parse(current_node.get());
        if (is_entry) {
            append_dynamic_selected_groups(defaults, selected_paths);
        }

        if (defaults.empty()) {
            current_node->remove_child(DefaultsListParser::DefaultsKey);
            return current_node;
        }

        current_node->remove_child(DefaultsListParser::DefaultsKey);
        return process_defaults_list(root_dir, canonical, current_node, defaults, selected_paths, dropped_groups);
    }

    void append_dynamic_selected_groups(
        std::vector<DefaultsEntry>& defaults,
        const std::unordered_map<std::string, std::string>& selected_paths) const {
        for (const auto& [raw_key, val] : selected_paths) {
            if (starts_with(raw_key, "++")) {
                std::string force_group = raw_key.substr(2);
                std::string fg_group, fg_pkg;
                DefaultsListParser::split_package(force_group, fg_group, fg_pkg);
                if (is_known_group(fg_group)) {
                    throw ConfigSyntaxException("Force-add ('++') of config groups is not supported: '" + raw_key + "=" + val + "'.");
                }
            }

            std::string key = raw_key;
            while (!key.empty() && key.front() == '+') key.erase(key.begin());
            std::string group, pkg;
            DefaultsListParser::split_package(key, group, pkg);

            bool already_in_defaults = false;
            for (const auto& d : defaults) {
                if (d.is_self) continue;
                if (d.matches_override_key(key) ||
                    (d.group == group && d.package == pkg)) {
                    already_in_defaults = true;
                    break;
                }
            }

            if (!already_in_defaults && (!pkg.empty() || is_known_group(group))) {
                defaults.push_back(DefaultsEntry::group_choice(group, val, false, pkg));
            }
        }
    }

    std::shared_ptr<ConfigObjectNode> resolve_explicit_inheritance(
        const std::string& root_dir,
        const std::string& current_dir,
        const std::shared_ptr<ConfigObjectNode>& raw_node,
        const std::unordered_map<std::string, std::string>& selected_paths,
        const std::unordered_set<std::string>& dropped_groups) {

        std::vector<std::string> base_files;
        for (const auto& key : { "_inherits_", "_base_" }) {
            auto child = raw_node->get_child(key);
            if (!child) continue;

            if (child->is_value()) {
                base_files.push_back(child->as_value()->as_string());
            } else if (child->is_list()) {
                const auto* ln = child->as_list();
                for (size_t i = 0; i < ln->size(); ++i) {
                    if ((*ln)[i]) base_files.push_back((*ln)[i]->to_string());
                }
            }
            raw_node->remove_child(key);
        }

        if (base_files.empty()) return nullptr;

        std::shared_ptr<ConfigObjectNode> composed_base = nullptr;
        for (const auto& bf : base_files) {
            std::string resolved_path;
            if (fs::path(bf).is_absolute() && fs::exists(bf)) {
                resolved_path = bf;
            } else {
                std::string in_cur = find_file(current_dir, bf);
                if (!in_cur.empty()) {
                    resolved_path = in_cur;
                } else {
                    resolved_path = find_file_across_search_paths(bf);
                }
            }

            if (resolved_path.empty()) {
                throw ConfigInheritanceException("Base configuration file '" + bf + "' not found.", raw_node->source());
            }

            auto base = load_file_hierarchy(root_dir, resolved_path, selected_paths, dropped_groups, false);
            if (!composed_base) {
                composed_base = base;
            } else {
                composed_base = std::static_pointer_cast<ConfigObjectNode>(OperatorEngine::apply(composed_base, base));
            }
        }

        return composed_base;
    }

    std::string resolve_interpolated_default_path(
        const std::string& raw_path,
        const DefaultsEntry& current_entry,
        const std::shared_ptr<ConfigObjectNode>& composed_so_far,
        const std::shared_ptr<ConfigObjectNode>& entry_self_node,
        const std::vector<DefaultsEntry>& all_defaults,
        const std::unordered_map<std::string, std::string>& selected_paths,
        const std::unordered_map<std::string, std::string>& resolved_group_choices) {

        // Detect circular interpolation across defaults entries
        for (const auto& other : all_defaults) {
            if (other.is_self || other.group.empty()) continue;
            if (&other == &current_entry) continue;
            if (raw_path.find("${" + other.selection_key() + "}") != std::string::npos ||
                raw_path.find("${" + other.group + "}") != std::string::npos) {
                auto it_ov = selected_paths.find(other.selection_key());
                std::string other_path = it_ov != selected_paths.end() ? it_ov->second : other.path;
                if (!other_path.empty() &&
                    (other_path.find("${" + current_entry.selection_key() + "}") != std::string::npos ||
                     other_path.find("${" + current_entry.group + "}") != std::string::npos)) {
                    throw ConfigInterpolationException(
                        "Circular interpolation detected in defaults list between '" + current_entry.selection_key() + "' and '" + other.selection_key() + "'.",
                        composed_so_far->source());
                }
            }
        }

        // Build evaluation context combining composed properties, _self_ properties, earlier resolved choices, and selected_paths
        auto context_node = std::static_pointer_cast<ConfigObjectNode>(
            OperatorEngine::apply(composed_so_far->clone(), entry_self_node->clone()));

        for (const auto& [grp, ch] : resolved_group_choices) {
            auto ex = context_node->get_child(grp);
            if (!ex || ex->is_object()) {
                context_node->set_child(grp, std::make_shared<ConfigValueNode>(ch), true);
            }
        }
        for (const auto& [sel_key, sel_val] : selected_paths) {
            auto ex = context_node->get_child(sel_key);
            if (!ex || ex->is_value()) {
                context_node->set_child(sel_key, std::make_shared<ConfigValueNode>(sel_val), true);
            }
        }

        InterpolationEngine engine(context_node);
        std::string res = engine.resolve_string(raw_path, "defaults." + current_entry.selection_key(), composed_so_far->source());
        while (!res.empty() && (std::isspace(static_cast<unsigned char>(res.front())) || res.front() == '/')) res.erase(res.begin());
        while (!res.empty() && std::isspace(static_cast<unsigned char>(res.back()))) res.pop_back();
        return res;
    }

    std::shared_ptr<ConfigObjectNode> process_defaults_list(
        const std::string& root_dir,
        const std::string& /*entry_file*/,
        const std::shared_ptr<ConfigObjectNode>& entry_node,
        const std::vector<DefaultsEntry>& raw_defaults,
        const std::unordered_map<std::string, std::string>& selected_paths,
        const std::unordered_set<std::string>& dropped_groups) {

        // Normalize 'override' entries
        std::vector<DefaultsEntry> defaults;
        auto effective_selected_paths = selected_paths;

        for (const auto& entry : raw_defaults) {
            if (!entry.is_self && !entry.group.empty()) {
                auto it = std::find_if(defaults.begin(), defaults.end(), [&](const DefaultsEntry& d) {
                    return !d.is_self && (d.selection_key() == entry.selection_key() || (d.group == entry.group && d.package == entry.package));
                });

                if (entry.is_override) {
                    if (effective_selected_paths.find(entry.selection_key()) == effective_selected_paths.end() && !entry.path.empty()) {
                        effective_selected_paths[entry.selection_key()] = entry.path;
                    }
                    if (it != defaults.end()) {
                        it->path = entry.path;
                        it->is_optional = it->is_optional || entry.is_optional;
                        it->is_disabled = entry.is_disabled;
                        it->is_missing = entry.is_missing;
                        it->multi_paths = entry.multi_paths;
                        continue;
                    }
                } else if (it != defaults.end() && it->path != entry.path) {
                    throw ConfigSyntaxException(
                        "Multiple values for '" + entry.selection_key() + "' in defaults list ('" + it->path + "' and '" + entry.path + "'). "
                        "To override an earlier value, use 'override " + entry.selection_key() + ": " + entry.path + "'.",
                        entry_node->source());
                }
            }
            defaults.push_back(entry);
        }

        auto result = std::make_shared<ConfigObjectNode>(ConfigOperator::Assign, entry_node->source());
        bool self_merged = false;
        std::unordered_map<std::string, std::string> resolved_group_choices;

        auto is_group_dropped = [&](const DefaultsEntry& e) -> bool {
            if (dropped_groups.count(e.group) || dropped_groups.count("~" + e.group)) return true;
            if (dropped_groups.count(e.selection_key()) || dropped_groups.count("~" + e.selection_key())) return true;
            if (!e.raw_package.empty()) {
                std::string raw_sel = e.group + "@" + e.raw_package;
                if (dropped_groups.count(raw_sel) || dropped_groups.count("~" + raw_sel)) return true;
            }
            if (!e.path.empty()) {
                if (dropped_groups.count(e.group + "=" + e.path) || dropped_groups.count("~" + e.group + "=" + e.path)) return true;
                if (dropped_groups.count(e.selection_key() + "=" + e.path) || dropped_groups.count("~" + e.selection_key() + "=" + e.path)) return true;
            }
            return false;
        };

        for (const auto& entry : defaults) {
            if (entry.is_self) {
                result = std::static_pointer_cast<ConfigObjectNode>(OperatorEngine::apply(result, entry_node));
                self_merged = true;
                continue;
            }

            if (is_group_dropped(entry)) {
                continue;
            }

            // Check for override choice from selected_paths
            std::string choice = entry.path;
            auto it_sel = effective_selected_paths.find(entry.selection_key());
            if (it_sel != effective_selected_paths.end()) {
                choice = it_sel->second;
            } else {
                auto it_sel_plus = effective_selected_paths.find("+" + entry.selection_key());
                if (it_sel_plus != effective_selected_paths.end()) {
                    choice = it_sel_plus->second;
                } else {
                    for (const auto& [k, v] : effective_selected_paths) {
                        if (entry.matches_override_key(k)) {
                            choice = v;
                            break;
                        }
                    }
                }
            }

            if (entry.is_disabled || choice == "null") {
                continue;
            }

            if (choice.find("${") != std::string::npos) {
                choice = resolve_interpolated_default_path(choice, entry, result, entry_node, defaults, effective_selected_paths, resolved_group_choices);
            }

            if (choice == "???" || (entry.is_missing && choice.empty())) {
                throw MissingValueMarkerException(entry.group, entry_node->source());
            }

            resolved_group_choices[entry.selection_key()] = choice;
            if (entry.package.empty()) {
                resolved_group_choices[entry.group] = choice;
            }

            // Handle multi_paths list
            if (!entry.multi_paths.empty() && (choice.empty() || choice == entry.path)) {
                for (auto mp : entry.multi_paths) {
                    if (mp.find("${") != std::string::npos) {
                        mp = resolve_interpolated_default_path(mp, entry, result, entry_node, defaults, effective_selected_paths, resolved_group_choices);
                    }
                    std::string file_pkg_header;
                    auto loaded = load_group_choice(root_dir, entry.group, mp, effective_selected_paths, dropped_groups, entry.is_optional, file_pkg_header);
                    if (!loaded) continue;
                    std::string target_pkg = !entry.package.empty() ? entry.package : (!file_pkg_header.empty() ? file_pkg_header : entry.group);
                    mount_package_node(result, target_pkg, loaded);
                }
                continue;
            }

            if (choice.empty()) {
                continue;
            }

            // Load configuration group
            std::string file_pkg_header;
            auto loaded_config = load_group_choice(root_dir, entry.group, choice, effective_selected_paths, dropped_groups, entry.is_optional, file_pkg_header);
            if (!loaded_config) continue;

            // Mount onto result
            std::string target_pkg = !entry.package.empty() ? entry.package : (!file_pkg_header.empty() ? file_pkg_header : entry.group);
            mount_package_node(result, target_pkg, loaded_config);
        }

        if (!self_merged) {
            result = std::static_pointer_cast<ConfigObjectNode>(OperatorEngine::apply(result, entry_node));
        }

        return result;
    }

    static void mount_package_node(const std::shared_ptr<ConfigObjectNode>& root,
                                   const std::string& target_package,
                                   const std::shared_ptr<ConfigObjectNode>& group_node) {
        if (target_package == DefaultsEntry::GlobalPackage || target_package == "_global_") {
            for (const auto& key : group_node->keys()) {
                auto child = group_node->get_child(key);
                auto existing = root->get_child(key);
                if (existing) {
                    root->set_child(key, OperatorEngine::apply(existing, child), true);
                } else {
                    root->set_child(key, child->clone(), true);
                }
            }
            return;
        }

        std::string pkg = target_package;
        std::replace(pkg.begin(), pkg.end(), '/', '.');

        std::vector<std::string> parts;
        size_t start = 0;
        while (start < pkg.size()) {
            size_t dot = pkg.find('.', start);
            std::string part = dot == std::string::npos ? pkg.substr(start) : pkg.substr(start, dot - start);
            if (!part.empty()) parts.push_back(std::move(part));
            if (dot == std::string::npos) break;
            start = dot + 1;
        }

        if (parts.empty()) return;

        ConfigObjectNode* current = root.get();
        for (size_t i = 0; i + 1 < parts.size(); ++i) {
            auto next = current->get_child(parts[i]);
            if (!next || !next->is_object()) {
                auto new_obj = std::make_shared<ConfigObjectNode>();
                current->set_child(parts[i], new_obj, true);
                current = new_obj.get();
            } else {
                current = next->as_object();
            }
        }

        const std::string& last_key = parts.back();
        auto existing = current->get_child(last_key);
        if (existing) {
            current->set_child(last_key, OperatorEngine::apply(existing, group_node), true);
        } else {
            current->set_child(last_key, group_node, true);
        }
    }

    std::shared_ptr<ConfigObjectNode> load_group_choice(
        const std::string& root_dir,
        const std::string& group,
        const std::string& choice,
        const std::unordered_map<std::string, std::string>& selected_paths,
        const std::unordered_set<std::string>& dropped_groups,
        bool is_optional,
        std::string& file_pkg_header) {

        file_pkg_header = "";

        // Check ConfigStore first
        auto stored = ConfigStore::instance().try_load(group, choice);
        if (stored && stored->node()) {
            if (stored->package().has_value()) {
                file_pkg_header = *stored->package();
            }
            return std::static_pointer_cast<ConfigObjectNode>(stored->node()->clone());
        }

        fs::path choice_path(choice);
        std::string filename_only = choice_path.filename().string();
        std::shared_ptr<ConfigObjectNode> accumulated_cascade = nullptr;

        std::string target_file_rel = (fs::path(group) / choice).string();
        std::string target_file = find_file_across_search_paths(target_file_rel);
        if (target_file.empty()) {
            target_file = find_file_across_search_paths(choice);
        }

        if (target_file.empty()) {
            if (is_optional) return nullptr;
            throw ConfigInheritanceException("Could not find configuration file for group '" + group + "' with choice '" + choice + "'.");
        }

        if (choice_path.has_parent_path()) {
            fs::path current_dir = group;
            for (const auto& segment : choice_path.parent_path()) {
                std::string parent_cand = find_file_across_search_paths((current_dir / filename_only).string());
                if (!parent_cand.empty() && fs::canonical(parent_cand) != fs::canonical(target_file)) {
                    auto layer_node = load_file_hierarchy(root_dir, parent_cand, selected_paths, dropped_groups, false);
                    accumulated_cascade = accumulated_cascade ?
                        std::static_pointer_cast<ConfigObjectNode>(OperatorEngine::apply(accumulated_cascade, layer_node)) :
                        layer_node;
                }
                current_dir /= segment;
            }
        }

        auto pkg_header = extract_file_package_header(target_file);
        if (pkg_header.has_value()) {
            file_pkg_header = *pkg_header;
        }

        auto target_node = load_file_hierarchy(root_dir, target_file, selected_paths, dropped_groups, false);

        if (accumulated_cascade) {
            return std::static_pointer_cast<ConfigObjectNode>(OperatorEngine::apply(accumulated_cascade, target_node));
        }

        return target_node;
    }
};

} // namespace omegaconf
