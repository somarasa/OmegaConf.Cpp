#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <algorithm>
#include <cctype>

namespace omegaconf {

/**
 * @brief Represents a single entry in a Hydra-style defaults: configuration list.
 */
class DefaultsEntry {
public:
    static constexpr std::string_view SelfMarker = "_self_";
    static constexpr std::string_view GlobalPackage = "_global_";
    static constexpr std::string_view GroupPackage = "_group_";

    std::string group{};
    std::string path{};
    std::string package{};
    std::string raw_package{};
    bool is_self{false};
    bool is_optional{false};
    bool is_override{false};
    bool is_missing{false};
    bool is_disabled{false};
    bool is_rooted{false};
    bool is_bare_single{false};
    std::vector<std::string> multi_paths{};

    [[nodiscard]] static DefaultsEntry make_self() {
        DefaultsEntry entry;
        entry.is_self = true;
        return entry;
    }

    [[nodiscard]] static DefaultsEntry make_group_choice(std::string group_in,
                                                         std::string path_in,
                                                         bool is_optional = false,
                                                         std::string package_in = "",
                                                         bool is_override = false,
                                                         bool is_missing = false,
                                                         bool is_disabled = false,
                                                         std::vector<std::string> multi_paths = {},
                                                         bool is_rooted_in = false,
                                                         bool is_bare_single_in = false) {
        DefaultsEntry entry;
        std::string group = std::move(group_in);
        while (!group.empty() && group.front() == '/') {
            group.erase(group.begin());
            is_rooted_in = true;
        }

        entry.group = std::move(group);
        entry.path = std::move(path_in);
        entry.is_optional = is_optional;
        entry.is_override = is_override;
        entry.is_missing = is_missing;
        entry.is_disabled = is_disabled;
        entry.multi_paths = std::move(multi_paths);
        entry.is_rooted = is_rooted_in;
        entry.is_bare_single = is_bare_single_in;

        if (!package_in.empty()) {
            entry.raw_package = package_in;
            std::string effective_group = is_bare_single_in ? "" : entry.group;
            for (char& c : effective_group) {
                if (c == '/') c = '.';
            }

            if (package_in == GroupPackage) {
                entry.package = effective_group.empty() ? std::string(GlobalPackage) : effective_group;
            } else if (starts_with(package_in, std::string(GroupPackage) + ".")) {
                std::string suffix = package_in.substr(GroupPackage.size() + 1);
                entry.package = effective_group.empty() ? suffix : (effective_group + "." + suffix);
            } else {
                entry.package = std::move(package_in);
            }
        }

        return entry;
    }

    [[nodiscard]] static DefaultsEntry group_choice(std::string group,
                                                    std::string path,
                                                    bool is_optional = false,
                                                    std::string package = "") {
        return make_group_choice(std::move(group), std::move(path), is_optional, std::move(package));
    }

    [[nodiscard]] std::string selection_key() const {
        if (package.empty()) return group;
        return group + "@" + package;
    }

    [[nodiscard]] bool matches_override_key(std::string_view candidate_key) const {
        std::string_view clean = candidate_key;
        while (!clean.empty() && (clean.front() == '+' || clean.front() == '/')) clean.remove_prefix(1);

        if (iequals(clean, selection_key())) return true;
        if (package.empty() && iequals(clean, group)) return true;

        if (!raw_package.empty()) {
            std::string raw_selection = group.empty() ? "@" + raw_package : group + "@" + raw_package;
            if (iequals(clean, raw_selection)) return true;
            if (iequals(raw_package, GroupPackage) && iequals(clean, group)) return true;
        }

        return false;
    }

private:
    static bool iequals(std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }
};

} // namespace omegaconf
