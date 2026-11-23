#pragma once

#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"

#include <string>
#include <string_view>
#include <memory>
#include <variant>
#include <optional>

namespace omegaconf {

using ResolvedValue = std::variant<ScalarValue, std::shared_ptr<ConfigNode>>;

/**
 * @brief Context interface provided to resolvers during interpolation.
 */
class IInterpolationContext {
public:
    virtual ~IInterpolationContext() = default;

    [[nodiscard]] virtual std::shared_ptr<ConfigNode> resolve_node(std::string_view path) = 0;
    [[nodiscard]] virtual std::string resolve_custom(std::string_view resolver_name, std::string_view argument) = 0;
};

/**
 * @brief Defines a named interpolation resolver invoked via ${name:argument} syntax.
 */
class IResolver {
public:
    virtual ~IResolver() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::string resolve(std::string_view argument, IInterpolationContext& context) = 0;

    [[nodiscard]] virtual std::optional<ResolvedValue> resolve_typed(
        std::string_view /*argument*/,
        IInterpolationContext& /*context*/) {
        return std::nullopt;
    }
};

} // namespace omegaconf
