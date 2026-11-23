#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <optional>
#include <vector>
#include <unordered_map>
#include <variant>

#include "omegaconf/common/source_info.hpp"
#include "omegaconf/common/exceptions.hpp"
#include "omegaconf/ast/config_enums.hpp"
#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include "omegaconf/ast/config_list_node.hpp"
#include "omegaconf/ast/config_object_node.hpp"
#include "omegaconf/operators/operator_parser.hpp"
#include "omegaconf/operators/operator_engine.hpp"
#include "omegaconf/parsers/yaml_format_parser.hpp"
#include "omegaconf/serialization/yaml_serializer.hpp"
#include "omegaconf/serialization/json_serializer.hpp"
#include "omegaconf/binding/config_binder.hpp"
#include "omegaconf/composition/defaults_entry.hpp"
#include "omegaconf/composition/defaults_list_parser.hpp"
#include "omegaconf/composition/config_store.hpp"
#include "omegaconf/composition/tree_resolver.hpp"
#include "omegaconf/cli/cli_override_parser.hpp"
#include "omegaconf/cli/override_aliases.hpp"
#include "omegaconf/interpolation/resolver.hpp"
#include "omegaconf/interpolation/resolvers.hpp"
#include "omegaconf/interpolation/interpolation_engine.hpp"
#include "omegaconf/multirun/sweep_engine.hpp"
#include "omegaconf/callbacks/config_callbacks.hpp"
#include "omegaconf/diff/config_differ.hpp"
#include "omegaconf/builder/omega_conf_builder.hpp"

namespace omegaconf {

/// @brief Returns the version string of OmegaConf.Cpp.
[[nodiscard]] inline constexpr std::string_view version() noexcept {
    return "0.1.0";
}

/**
 * @brief Static utility facade inspired by Python's OmegaConf and hydra.utils APIs.
 */
class OmegaConfig {
public:
    static OmegaConfBuilder create() {
        return OmegaConfBuilder::create();
    }

    static std::shared_ptr<ConfigObjectNode> merge(
        const std::vector<std::shared_ptr<ConfigObjectNode>>& configs,
        bool resolve_interpolations = true) {
        auto merged = std::make_shared<ConfigObjectNode>();
        for (const auto& cfg : configs) {
            if (!cfg) continue;
            auto applied = OperatorEngine::apply(merged, cfg->clone());
            merged = std::static_pointer_cast<ConfigObjectNode>(applied);
        }

        if (resolve_interpolations) {
            InterpolationEngine engine(merged);
            engine.resolve_all();
        }

        return merged;
    }

    template <typename... Args>
    static std::shared_ptr<ConfigObjectNode> merge(const std::shared_ptr<ConfigObjectNode>& first, const Args&... rest) {
        std::vector<std::shared_ptr<ConfigObjectNode>> list = { first, rest... };
        return merge(list);
    }

    template <typename T = std::string>
    static T select(const std::shared_ptr<ConfigObjectNode>& root, std::string_view path, T default_value = T{}) {
        if (!root) return default_value;
        return root->template select<T>(path, default_value);
    }

    static void set_read_only(const std::shared_ptr<ConfigNode>& node, bool read_only = true) {
        if (node) node->set_read_only(read_only);
    }

    static void set_struct(const std::shared_ptr<ConfigObjectNode>& node, bool is_struct = true) {
        if (node) node->set_struct(is_struct);
    }

    static std::shared_ptr<ConfigObjectNode> parse_yaml(std::string_view yaml_content) {
        return YamlFormatParser::parse(yaml_content);
    }

    static std::shared_ptr<ConfigObjectNode> parse_json(std::string_view json_content) {
        return YamlFormatParser::parse(json_content);
    }

    static std::string to_yaml(const std::shared_ptr<ConfigNode>& node) {
        return node ? node->to_yaml() : "null\n";
    }

    static std::string to_json(const std::shared_ptr<ConfigNode>& node, bool indented = false) {
        return node ? node->to_json(indented) : "null";
    }
};

using OmegaConf = OmegaConfig;

} // namespace omegaconf

#include "omegaconf/ast/config_node_impl.hpp"

