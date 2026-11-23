#pragma once

#include "omegaconf/ast/config_node.hpp"
#include <vector>
#include <memory>
#include <string>
#include <sstream>

namespace omegaconf {

/**
 * @brief Represents an ordered list/sequence of configuration nodes.
 * Supports += (append items) and -= (remove matching items) mutation operators.
 */
class ConfigListNode : public ConfigNode {
public:
    explicit ConfigListNode(ConfigOperator op = ConfigOperator::Assign, SourceInfo source = SourceInfo::unknown()) {
        op_ = op;
        source_ = std::move(source);
    }

    explicit ConfigListNode(std::vector<std::shared_ptr<ConfigNode>> items,
                            ConfigOperator op = ConfigOperator::Assign,
                            SourceInfo source = SourceInfo::unknown())
        : items_(std::move(items)) {
        op_ = op;
        source_ = std::move(source);
    }

    [[nodiscard]] ConfigNodeType node_type() const noexcept override {
        return ConfigNodeType::List;
    }

    [[nodiscard]] const std::vector<std::shared_ptr<ConfigNode>>& items() const noexcept {
        return items_;
    }

    [[nodiscard]] std::vector<std::shared_ptr<ConfigNode>>& items() noexcept {
        return items_;
    }

    [[nodiscard]] size_t size() const noexcept {
        return items_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return items_.empty();
    }

    void add_item(std::shared_ptr<ConfigNode> item) {
        ensure_writable();
        items_.push_back(std::move(item));
    }

    void remove_at(size_t index) {
        ensure_writable();
        if (index < items_.size()) {
            items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

    [[nodiscard]] std::shared_ptr<ConfigNode>& operator[](size_t index) {
        return items_[index];
    }

    [[nodiscard]] const std::shared_ptr<ConfigNode>& operator[](size_t index) const {
        return items_[index];
    }

    [[nodiscard]] std::shared_ptr<ConfigNode>& at(size_t index) {
        return items_.at(index);
    }

    [[nodiscard]] const std::shared_ptr<ConfigNode>& at(size_t index) const {
        return items_.at(index);
    }

    void set_read_only(bool read_only = true) override {
        ConfigNode::set_read_only(read_only);
        for (auto& item : items_) {
            if (item) {
                item->set_read_only(read_only);
            }
        }
    }

    [[nodiscard]] std::shared_ptr<ConfigNode> clone() const override {
        std::vector<std::shared_ptr<ConfigNode>> cloned_items;
        cloned_items.reserve(items_.size());
        for (const auto& item : items_) {
            cloned_items.push_back(item ? item->clone() : nullptr);
        }
        return std::make_shared<ConfigListNode>(std::move(cloned_items), op_, source_);
    }

    [[nodiscard]] std::string to_string() const override {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < items_.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << (items_[i] ? items_[i]->to_string() : "null");
        }
        ss << "]";
        return ss.str();
    }

private:
    std::vector<std::shared_ptr<ConfigNode>> items_{};
};

} // namespace omegaconf
