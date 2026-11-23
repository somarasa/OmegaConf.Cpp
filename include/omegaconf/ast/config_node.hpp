#pragma once

#include "omegaconf/common/source_info.hpp"
#include "omegaconf/common/exceptions.hpp"
#include "omegaconf/ast/config_enums.hpp"
#include <memory>
#include <string>

namespace omegaconf {

class ConfigValueNode;
class ConfigListNode;
class ConfigObjectNode;

/**
 * @brief Abstract base node in the OmegaConf Abstract Syntax Tree (AST).
 * Tracks origin file/line metadata (source), any pending mutation operator (operator),
 * and immutability state (is_read_only).
 */
class ConfigNode : public std::enable_shared_from_this<ConfigNode> {
public:
    virtual ~ConfigNode() = default;

    [[nodiscard]] virtual ConfigNodeType node_type() const noexcept = 0;

    [[nodiscard]] const SourceInfo& source() const noexcept { return source_; }
    void set_source(SourceInfo source) noexcept { source_ = std::move(source); }

    [[nodiscard]] ConfigOperator op() const noexcept { return op_; }
    void set_op(ConfigOperator op) noexcept { op_ = op; }

    [[nodiscard]] bool is_read_only() const noexcept { return is_read_only_; }

    virtual void set_read_only(bool read_only = true) {
        is_read_only_ = read_only;
    }

    void freeze() {
        set_read_only(true);
    }

    [[nodiscard]] virtual std::shared_ptr<ConfigNode> clone() const = 0;
    [[nodiscard]] virtual std::string to_string() const = 0;
    [[nodiscard]] virtual std::string to_yaml() const;
    [[nodiscard]] virtual std::string to_json(bool indented = false) const;
    [[nodiscard]] virtual std::string to_toml() const;

    [[nodiscard]] bool is_value() const noexcept { return node_type() == ConfigNodeType::Value; }
    [[nodiscard]] bool is_list() const noexcept { return node_type() == ConfigNodeType::List; }
    [[nodiscard]] bool is_object() const noexcept { return node_type() == ConfigNodeType::Object; }

    [[nodiscard]] ConfigValueNode* as_value() noexcept;
    [[nodiscard]] const ConfigValueNode* as_value() const noexcept;

    [[nodiscard]] ConfigListNode* as_list() noexcept;
    [[nodiscard]] const ConfigListNode* as_list() const noexcept;

    [[nodiscard]] ConfigObjectNode* as_object() noexcept;
    [[nodiscard]] const ConfigObjectNode* as_object() const noexcept;

protected:
    SourceInfo source_{SourceInfo::unknown()};
    ConfigOperator op_{ConfigOperator::Assign};
    bool is_read_only_{false};

    void ensure_writable() const {
        if (is_read_only_) {
            throw ConfigReadOnlyException("Cannot modify a read-only configuration node.", source_);
        }
    }
};

} // namespace omegaconf
