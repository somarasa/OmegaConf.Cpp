#pragma once

#include "omegaconf/ast/config_object_node.hpp"
#include "omegaconf/composition/tree_resolver.hpp"
#include "omegaconf/cli/cli_override_parser.hpp"
#include "omegaconf/cli/override_aliases.hpp"
#include "omegaconf/interpolation/interpolation_engine.hpp"
#include "omegaconf/binding/config_binder.hpp"
#include "omegaconf/multirun/sweep_engine.hpp"
#include "omegaconf/callbacks/config_callbacks.hpp"
#include "omegaconf/composition/config_store.hpp"

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <filesystem>
#include <fstream>
#include <initializer_list>

namespace fs = std::filesystem;

namespace omegaconf {

class OmegaConfBuilder {
public:
    static OmegaConfBuilder create() {
        return OmegaConfBuilder{};
    }

    OmegaConfBuilder& set_root_directory(std::string root_dir) {
        root_dir_ = std::move(root_dir);
        return *this;
    }

    OmegaConfBuilder& set_root(std::string root_dir) {
        return set_root_directory(std::move(root_dir));
    }

    OmegaConfBuilder& set_entry_file(std::string entry_file) {
        entry_file_ = std::move(entry_file);
        return *this;
    }

    OmegaConfBuilder& add_search_path(std::string path) {
        search_paths_.push_back(std::move(path));
        return *this;
    }

    OmegaConfBuilder& select_path(std::string group, std::string choice) {
        selected_paths_[std::move(group)] = std::move(choice);
        return *this;
    }

    OmegaConfBuilder& drop_group(std::string group) {
        dropped_groups_.insert(std::move(group));
        return *this;
    }

    OmegaConfBuilder& with_struct_mode(bool struct_mode = true) {
        struct_mode_ = struct_mode;
        return *this;
    }

    OmegaConfBuilder& with_output_directory(std::string out_dir) {
        output_directory_ = std::move(out_dir);
        return *this;
    }

    OmegaConfBuilder& add_cli_arg(std::string arg) {
        cli_args_.push_back(std::move(arg));
        return *this;
    }

    OmegaConfBuilder& add_cli_args(const std::vector<std::string>& args) {
        for (const auto& a : args) {
            cli_args_.push_back(a);
        }
        return *this;
    }

    template <typename... Args>
    OmegaConfBuilder& add_cli_args(const std::string& first, const Args&... rest) {
        cli_args_.push_back(first);
        (cli_args_.push_back(rest), ...);
        return *this;
    }

    OmegaConfBuilder& add_alias(std::string name, std::string target) {
        builder_aliases_[std::move(name)] = std::move(target);
        return *this;
    }

    OmegaConfBuilder& with_config_store(const ConfigStore& /*store*/) {
        return *this;
    }

    static std::string_view trim_sv(std::string_view s) noexcept {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    }

    bool try_handle_cli_inspection(std::string& output) {
        output.clear();
        std::string mode{};
        std::string sub_arg{};
        std::vector<std::string> override_args;

        for (size_t i = 0; i < cli_args_.size(); ++i) {
            std::string arg = std::string(trim_sv(cli_args_[i]));
            if (arg == "--help" || arg == "-h") {
                mode = "help";
            } else if (arg == "--cfg" || arg == "-c") {
                mode = "cfg";
                if (i + 1 < cli_args_.size() && !starts_with(cli_args_[i + 1], '-') && cli_args_[i + 1].find('=') == std::string::npos) {
                    sub_arg = std::string(trim_sv(cli_args_[++i]));
                } else {
                    sub_arg = "all";
                }
            } else if (arg == "--info" || arg == "-i") {
                mode = "info";
                if (i + 1 < cli_args_.size() && !starts_with(cli_args_[i + 1], '-') && cli_args_[i + 1].find('=') == std::string::npos) {
                    sub_arg = std::string(trim_sv(cli_args_[++i]));
                } else {
                    sub_arg = "all";
                }
            } else if (arg != "-m" && arg != "--multirun") {
                override_args.push_back(arg);
            }
        }

        if (mode.empty()) return false;

        std::shared_ptr<ConfigObjectNode> raw_entry = nullptr;
        try {
            fs::path p = fs::path(root_dir_) / entry_file_;
            if (fs::exists(p)) {
                std::ifstream ifs(p);
                std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                raw_entry = YamlFormatParser::parse(content, p.string());
            }
        } catch (...) {}

        auto aliases = OverrideAliases::from_config(raw_entry, builder_aliases_);

        if (mode == "info" && (sub_arg == "aliases" || sub_arg == "Aliases")) {
            output = aliases.is_empty() ? "No override aliases defined" : aliases.format(override_args);
            return true;
        }

        if (mode == "help") {
            std::ostringstream ss;
            ss << "== Configuration Help (" << entry_file_ << ") ==\n\n";
            auto groups = discover_config_groups();
            if (!groups.empty()) {
                ss << "== Configuration Groups ==\n";
                for (const auto& [grp, choices] : groups) {
                    ss << "  " << grp << ": ";
                    for (size_t c = 0; c < choices.size(); ++c) {
                        if (c > 0) ss << ", ";
                        ss << choices[c];
                    }
                    ss << "\n";
                }
                ss << "\n";
            }
            if (!aliases.is_empty()) {
                ss << aliases.format(override_args) << "\n";
            }
            output = ss.str();
            while (!output.empty() && std::isspace(static_cast<unsigned char>(output.back()))) output.pop_back();
            return true;
        }

        if (mode == "cfg") {
            auto clone = *this;
            clone.cli_args_ = override_args;
            auto resolved = clone.build_node();
            output = resolved->to_yaml();
            while (!output.empty() && std::isspace(static_cast<unsigned char>(output.back()))) output.pop_back();
            return true;
        }

        return false;
    }

    OmegaConfBuilder& add_callback(std::shared_ptr<IConfigCallback> callback) {
        if (callback) {
            callbacks_.push_back(std::move(callback));
        }
        return *this;
    }

    OmegaConfBuilder& register_resolver(std::shared_ptr<IResolver> resolver) {
        if (resolver) {
            custom_resolvers_.push_back(std::move(resolver));
        }
        return *this;
    }

    OmegaConfBuilder& register_resolver(const std::string& name, std::function<std::string(std::string_view)> func) {
        custom_resolvers_.push_back(std::make_shared<CustomResolver>(name, std::move(func)));
        return *this;
    }

    std::shared_ptr<ConfigObjectNode> build_node() {
        // Step 0: Override aliases expansion
        auto expanded_args = expand_aliases(cli_args_);

        for (const auto& cb : callbacks_) {
            cb->on_run_start(nullptr, entry_file_);
            cb->on_job_start(nullptr, 0);
        }

        auto node = build_single_node(expanded_args);

        if (output_directory_.has_value() && !output_directory_->empty()) {
            write_single_run_snapshot(*output_directory_, node, expanded_args);
        }

        for (auto it = callbacks_.rbegin(); it != callbacks_.rend(); ++it) {
            (*it)->on_job_end(node, 0, nullptr);
            (*it)->on_run_end(node, entry_file_);
        }

        return node;
    }

    template <typename T>
    T build() {
        auto node = build_node();
        return ConfigBinder::bind<T>(node);
    }

    std::vector<std::shared_ptr<ConfigObjectNode>> build_sweep_nodes() {
        auto expanded_args = expand_aliases(cli_args_);
        auto sweep_res = SweepEngine::parse_sweep_args(expanded_args, root_dir_);

        auto combinations = SweepEngine::generate_cartesian_product(sweep_res.sweep_matrix, sweep_res.sweep_keys_order);

        for (const auto& cb : callbacks_) {
            cb->on_multirun_start(nullptr, entry_file_);
        }

        std::vector<std::shared_ptr<ConfigObjectNode>> sweep_results;
        sweep_results.reserve(combinations.size());

        if (output_directory_.has_value() && !output_directory_->empty()) {
            fs::create_directories(*output_directory_);
            std::ofstream mr_file(fs::path(*output_directory_) / "multirun.yaml");
            mr_file << "# Hydra MultiRun Matrix\nruns: " << combinations.size() << "\n";
        }

        for (size_t job_id = 0; job_id < combinations.size(); ++job_id) {
            const auto& comb = combinations[job_id];
            std::vector<std::string> job_args = sweep_res.non_sweep_args;
            for (const auto& [k, v] : comb) {
                job_args.push_back(k + "=" + v);
            }

            for (const auto& cb : callbacks_) {
                cb->on_job_start(nullptr, static_cast<int>(job_id));
            }

            auto job_node = build_single_node(job_args);

            if (output_directory_.has_value() && !output_directory_->empty()) {
                fs::path job_out = fs::path(*output_directory_) / std::to_string(job_id);
                write_single_run_snapshot(job_out.string(), job_node, job_args);
            }

            for (auto it = callbacks_.rbegin(); it != callbacks_.rend(); ++it) {
                (*it)->on_job_end(job_node, static_cast<int>(job_id), nullptr);
            }

            sweep_results.push_back(std::move(job_node));
        }

        for (auto it = callbacks_.rbegin(); it != callbacks_.rend(); ++it) {
            (*it)->on_multirun_end(sweep_results, entry_file_);
        }

        return sweep_results;
    }

    template <typename T>
    std::vector<T> build_sweep() {
        auto nodes = build_sweep_nodes();
        std::vector<T> results;
        results.reserve(nodes.size());
        for (const auto& n : nodes) {
            results.push_back(ConfigBinder::bind<T>(n));
        }
        return results;
    }

private:
    std::string root_dir_{ "." };
    std::string entry_file_{ "config.yaml" };
    std::vector<std::string> search_paths_{};
    std::unordered_map<std::string, std::string> selected_paths_{};
    std::unordered_set<std::string> dropped_groups_{};
    std::vector<std::string> cli_args_{};
    std::vector<std::shared_ptr<IResolver>> custom_resolvers_{};
    std::map<std::string, std::string> builder_aliases_{};
    std::vector<std::shared_ptr<IConfigCallback>> callbacks_{};
    bool struct_mode_{ false };
    std::optional<std::string> output_directory_{ std::nullopt };

    std::vector<std::string> expand_aliases(const std::vector<std::string>& raw_args) {
        // Try reading raw primary entry
        std::shared_ptr<ConfigObjectNode> raw_entry = nullptr;
        try {
            fs::path p = fs::path(root_dir_) / entry_file_;
            if (fs::exists(p)) {
                std::ifstream ifs(p);
                std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                raw_entry = YamlFormatParser::parse(content, p.string());
            }
        } catch (...) {}

        auto aliases = OverrideAliases::from_config(raw_entry, builder_aliases_);
        if (aliases.is_empty()) {
            return raw_args;
        }

        OverrideAliases::validate_cli_does_not_modify_aliases(raw_args);

        std::vector<std::string> expanded;
        for (const auto& a : raw_args) {
            expanded.push_back(aliases.expand(a));
        }
        return expanded;
    }

    bool has_config_aliases() const {
        return !builder_aliases_.empty();
    }

    std::shared_ptr<ConfigObjectNode> build_single_node(const std::vector<std::string>& args) {
        auto active_selected_paths = selected_paths_;
        std::unordered_set<std::string> active_dropped_groups = dropped_groups_;
        std::vector<std::string> property_overrides;

        for (const auto& arg : args) {
            std::string_view raw(arg);
            while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.remove_prefix(1);
            while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.remove_suffix(1);

            if (raw.empty()) continue;

            if (starts_with(raw, '~')) {
                std::string target(raw.substr(1));
                size_t eq_idx = target.find('=');
                std::string group_cand = eq_idx == std::string::npos ? target : target.substr(0, eq_idx);
                size_t at_idx = group_cand.find('@');
                std::string clean_group = at_idx != std::string::npos ? group_cand.substr(0, at_idx) : group_cand;

                if (is_group_directory(clean_group) || ConfigStore::instance().has_group(clean_group)) {
                    active_dropped_groups.insert(target);
                    continue;
                }
                property_overrides.push_back(arg);
                continue;
            }

            size_t eq_pos = raw.find('=');
            if (eq_pos != std::string_view::npos && eq_pos > 0 &&
                raw[eq_pos - 1] != '+' && raw[eq_pos - 1] != '-' &&
                raw[eq_pos - 1] != '*' && raw[eq_pos - 1] != '/' &&
                raw[eq_pos - 1] != '&' && raw[eq_pos - 1] != '|' && raw[eq_pos - 1] != '!') {

                std::string_view key_part = raw.substr(0, eq_pos);
                std::string_view val_part = raw.substr(eq_pos + 1);

                std::string_view clean_key = key_part;
                while (!clean_key.empty() && clean_key.front() == '+') clean_key.remove_prefix(1);

                std::string group_name, pkg;
                DefaultsListParser::split_package(clean_key, group_name, pkg);

                if (is_group_directory(group_name) || ConfigStore::instance().has_group(group_name) || !pkg.empty()) {
                    active_selected_paths[std::string(key_part)] = std::string(val_part);
                    continue;
                }
            }

            property_overrides.push_back(arg);
        }

        // Tree composition
        TreeResolver tree_resolver;
        auto root = tree_resolver.resolve(root_dir_, entry_file_, active_selected_paths, active_dropped_groups, search_paths_);

        if (struct_mode_) {
            root->set_struct(true);
        }

        // CLI property overrides
        CliOverrideParser::apply_overrides(root, property_overrides, struct_mode_);

        // Variable interpolation
        InterpolationEngine engine(root, custom_resolvers_);
        engine.resolve_all();

        for (const auto& cb : callbacks_) {
            cb->on_compose_config(root, entry_file_, args);
        }

        return root;
    }

    void write_single_run_snapshot(const std::string& dir,
                                   const std::shared_ptr<ConfigObjectNode>& node,
                                   const std::vector<std::string>& overrides) {
        fs::path oc_dir = fs::path(dir) / ".omegaconf";
        fs::create_directories(oc_dir);

        std::ofstream cfg_file(oc_dir / "config.yaml");
        cfg_file << node->to_yaml();

        std::ofstream ovr_file(oc_dir / "overrides.yaml");
        for (const auto& o : overrides) {
            ovr_file << "- " << o << "\n";
        }
    }

    std::map<std::string, std::vector<std::string>> discover_config_groups() const {
        std::map<std::string, std::vector<std::string>> result;
        std::vector<std::string> all_dirs = { root_dir_ };
        all_dirs.insert(all_dirs.end(), search_paths_.begin(), search_paths_.end());

        for (const auto& dir : all_dirs) {
            if (!fs::exists(dir) || !fs::is_directory(dir)) continue;
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (entry.is_directory()) {
                    std::string grp_name = entry.path().filename().string();
                    if (starts_with(grp_name, '.') || grp_name == "build") continue;
                    std::vector<std::string> choices;
                    for (const auto& sub : fs::directory_iterator(entry.path())) {
                        if (!sub.is_directory()) {
                            std::string stem = sub.path().stem().string();
                            choices.push_back(stem);
                        }
                    }
                    std::sort(choices.begin(), choices.end());
                    result[grp_name] = std::move(choices);
                }
            }
        }
        return result;
    }

    bool is_group_directory(const std::string& name) const {
        if (std::filesystem::exists(std::filesystem::path(root_dir_) / name) &&
            std::filesystem::is_directory(std::filesystem::path(root_dir_) / name)) {
            return true;
        }
        for (const auto& sp : search_paths_) {
            if (std::filesystem::exists(std::filesystem::path(sp) / name) &&
                std::filesystem::is_directory(std::filesystem::path(sp) / name)) {
                return true;
            }
        }
        return false;
    }
};

} // namespace omegaconf
