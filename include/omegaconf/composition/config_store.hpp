#pragma once

#include "omegaconf/ast/config_object_node.hpp"
#include "omegaconf/common/exceptions.hpp"
#include "omegaconf/common/source_info.hpp"
#include "omegaconf/composition/defaults_entry.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory>
#include <optional>
#include <algorithm>
#include <stdexcept>

namespace omegaconf {

enum class ConfigStoreEntryType {
    NotFound,
    Group,
    Config
};

class StoredConfigNode {
public:
    StoredConfigNode(std::string name,
                     std::optional<std::string> group,
                     std::shared_ptr<ConfigObjectNode> node,
                     std::optional<std::string> package = std::nullopt,
                     std::optional<std::string> provider = std::nullopt)
        : name_(std::move(name)),
          group_(std::move(group)),
          node_(std::move(node)),
          package_(std::move(package)),
          provider_(std::move(provider)) {}

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::optional<std::string>& group() const noexcept { return group_; }
    [[nodiscard]] const std::shared_ptr<ConfigObjectNode>& node() const noexcept { return node_; }
    [[nodiscard]] const std::optional<std::string>& package() const noexcept { return package_; }
    [[nodiscard]] const std::optional<std::string>& provider() const noexcept { return provider_; }

    [[nodiscard]] std::shared_ptr<StoredConfigNode> clone() const {
        auto cloned_node = node_ ? std::static_pointer_cast<ConfigObjectNode>(node_->clone()) : nullptr;
        return std::make_shared<StoredConfigNode>(name_, group_, cloned_node, package_, provider_);
    }

private:
    std::string name_{};
    std::optional<std::string> group_{};
    std::shared_ptr<ConfigObjectNode> node_{};
    std::optional<std::string> package_{};
    std::optional<std::string> provider_{};
};

class ConfigStoreWithProvider;

class ConfigStore {
public:
    static ConfigStore& instance() {
        static ConfigStore inst;
        return inst;
    }

    ConfigStore() = default;

    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept {
        return warnings_;
    }

    [[nodiscard]] ConfigStoreWithProvider with_provider(std::string provider);

    void clear() {
        root_.subgroups.clear();
        root_.configs.clear();
        warnings_.clear();
    }

    void store(const std::string& name,
               std::shared_ptr<ConfigObjectNode> node,
               const std::optional<std::string>& group = std::nullopt,
               const std::optional<std::string>& package = std::nullopt,
               const std::optional<std::string>& provider = std::nullopt,
               std::optional<bool> replace = std::nullopt) {
        if (name.empty()) {
            throw ConfigSyntaxException("Config name cannot be empty in ConfigStore::store.");
        }
        if (!node) {
            throw ConfigSyntaxException("Config node cannot be null in ConfigStore::store.");
        }

        std::optional<std::string> normalized_group = normalize_group(group);
        std::string normalized_name = normalize_config_name(name);
        std::string full_key = normalized_group ? (*normalized_group + "/" + normalized_name) : normalized_name;

        GroupFolder& target_folder = get_or_create_group_folder(normalized_group);

        auto it = target_folder.configs.find(normalized_name);
        if (it != target_folder.configs.end()) {
            if (replace.has_value() && !replace.value()) {
                throw ConfigSyntaxException("Config '" + full_key + "' is already registered in ConfigStore.");
            }
            if (!replace.has_value()) {
                std::string old_p = it->second->provider().value_or("null");
                std::string new_p = provider.value_or("null");
                warnings_.push_back("Overwriting '" + full_key + "' in ConfigStore (existing provider: " + old_p + ", new provider: " + new_p + ").");
            }
        }

        std::optional<std::string> expanded_pkg = package;
        if (package.has_value() && (package.value() == "_global_" || starts_with(package.value(), "_global_"))) {
            expanded_pkg = package.value() == "_global_" ? std::nullopt : std::optional<std::string>(package.value().substr(9));
        }

        auto cloned_node = std::static_pointer_cast<ConfigObjectNode>(node->clone());
        target_folder.configs[normalized_name] = std::make_shared<StoredConfigNode>(
            normalized_name, normalized_group, cloned_node, expanded_pkg, provider);
    }

    [[nodiscard]] std::shared_ptr<StoredConfigNode> load(const std::string& config_path) const {
        auto stored = try_load(config_path);
        if (stored) {
            return stored;
        }
        throw ConfigSyntaxException("Structured config '" + config_path + "' not found in ConfigStore.");
    }

    [[nodiscard]] std::shared_ptr<StoredConfigNode> try_load(const std::string& config_path) const {
        if (config_path.empty()) return nullptr;

        std::string normalized = config_path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        while (!normalized.empty() && normalized.front() == '/') normalized.erase(normalized.begin());
        while (!normalized.empty() && normalized.back() == '/') normalized.pop_back();

        size_t last_slash = normalized.rfind('/');
        std::optional<std::string> group = last_slash != std::string::npos ? std::optional<std::string>(normalized.substr(0, last_slash)) : std::nullopt;
        std::string raw_name = last_slash != std::string::npos ? normalized.substr(last_slash + 1) : normalized;
        std::string normalized_name = normalize_config_name(raw_name);

        const GroupFolder* folder = find_group_folder(group);
        if (folder) {
            auto it = folder->configs.find(normalized_name);
            if (it != folder->configs.end()) {
                return it->second->clone();
            }
        }

        return nullptr;
    }

    [[nodiscard]] std::shared_ptr<StoredConfigNode> try_load(const std::optional<std::string>& group, const std::string& name) const {
        if (!group.has_value() || group->empty()) {
            return try_load(name);
        }
        return try_load(*group + "/" + name);
    }

    [[nodiscard]] bool has_group(const std::string& group_path) const {
        return get_entry_type(group_path) == ConfigStoreEntryType::Group;
    }

    [[nodiscard]] ConfigStoreEntryType get_entry_type(const std::string& path) const {
        if (path.empty()) {
            return ConfigStoreEntryType::Group;
        }

        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        while (!normalized.empty() && normalized.front() == '/') normalized.erase(normalized.begin());
        while (!normalized.empty() && normalized.back() == '/') normalized.pop_back();

        if (find_group_folder(normalized) != nullptr) {
            return ConfigStoreEntryType::Group;
        }

        if (try_load(normalized) != nullptr) {
            return ConfigStoreEntryType::Config;
        }

        return ConfigStoreEntryType::NotFound;
    }

    [[nodiscard]] std::vector<std::string> list(const std::optional<std::string>& group_path = std::nullopt) const {
        std::optional<std::string> normalized = normalize_group(group_path);
        const GroupFolder* folder = find_group_folder(normalized);
        if (!folder) {
            if (normalized.has_value() && try_load(*normalized)) {
                throw ConfigSyntaxException("Path '" + group_path.value_or("") + "' points to a config node, not a group directory.");
            }
            throw ConfigSyntaxException("Group path '" + group_path.value_or("") + "' not found in ConfigStore.");
        }

        std::vector<std::string> entries;
        for (const auto& [k, _] : folder->subgroups) entries.push_back(k);
        for (const auto& [k, _] : folder->configs) entries.push_back(k);
        std::sort(entries.begin(), entries.end());
        return entries;
    }

private:
    struct GroupFolder {
        std::map<std::string, GroupFolder> subgroups{};
        std::map<std::string, std::shared_ptr<StoredConfigNode>> configs{};
    };

    GroupFolder root_{};
    std::vector<std::string> warnings_{};

    GroupFolder& get_or_create_group_folder(const std::optional<std::string>& normalized_group) {
        if (!normalized_group.has_value() || normalized_group->empty()) {
            return root_;
        }

        GroupFolder* current = &root_;
        std::string s = *normalized_group;
        size_t start = 0;
        while (start < s.size()) {
            size_t slash = s.find('/', start);
            std::string seg = slash == std::string::npos ? s.substr(start) : s.substr(start, slash - start);
            if (!seg.empty()) {
                current = &current->subgroups[seg];
            }
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        return *current;
    }

    [[nodiscard]] const GroupFolder* find_group_folder(const std::optional<std::string>& normalized_group) const {
        if (!normalized_group.has_value() || normalized_group->empty()) {
            return &root_;
        }

        const GroupFolder* current = &root_;
        std::string s = *normalized_group;
        size_t start = 0;
        while (start < s.size()) {
            size_t slash = s.find('/', start);
            std::string seg = slash == std::string::npos ? s.substr(start) : s.substr(start, slash - start);
            if (!seg.empty()) {
                auto it = current->subgroups.find(seg);
                if (it == current->subgroups.end()) {
                    return nullptr;
                }
                current = &it->second;
            }
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        return current;
    }

    static std::optional<std::string> normalize_group(const std::optional<std::string>& group) {
        if (!group.has_value() || group->empty()) return std::nullopt;
        std::string s = *group;
        std::replace(s.begin(), s.end(), '\\', '/');
        while (!s.empty() && s.front() == '/') s.erase(s.begin());
        while (!s.empty() && s.back() == '/') s.pop_back();
        if (s.empty()) return std::nullopt;
        return s;
    }

    static std::string normalize_config_name(std::string name) {
        std::replace(name.begin(), name.end(), '\\', '/');
        while (!name.empty() && name.front() == '/') name.erase(name.begin());
        if (ends_with(name, ".yaml")) return name;
        if (ends_with(name, ".yml")) return name.substr(0, name.size() - 4) + ".yaml";
        if (ends_with(name, ".json") || ends_with(name, ".jsonc") || ends_with(name, ".toml")) {
            size_t dot = name.rfind('.');
            return name.substr(0, dot) + ".yaml";
        }
        return name + ".yaml";
    }
};

class ConfigStoreWithProvider {
public:
    ConfigStoreWithProvider(ConfigStore& store, std::string provider)
        : store_(store), provider_(std::move(provider)) {}

    void store(const std::string& name,
               std::shared_ptr<ConfigObjectNode> node,
               const std::optional<std::string>& group = std::nullopt,
               const std::optional<std::string>& package = std::nullopt,
               const std::optional<std::string>& provider = std::nullopt,
               std::optional<bool> replace = std::nullopt) {
        store_.store(name, std::move(node), group, package, provider.has_value() ? provider : provider_, replace);
    }

private:
    ConfigStore& store_;
    std::string provider_{};
};

inline ConfigStoreWithProvider ConfigStore::with_provider(std::string provider) {
    return ConfigStoreWithProvider(*this, std::move(provider));
}

} // namespace omegaconf
