#pragma once

#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include "omegaconf/ast/config_list_node.hpp"
#include "omegaconf/common/exceptions.hpp"
#include <unordered_map>
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <cctype>
#include <sstream>
#include <algorithm>

namespace omegaconf {

struct CaseInsensitiveEqual {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept {
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

struct CaseInsensitiveHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const noexcept {
        size_t h = 2166136261u;
        for (char c : s) {
            h = (h ^ static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(c)))) * 16777619u;
        }
        return h;
    }
};

namespace detail {
    template <typename, typename = void>
    struct has_value_type : std::false_type {};
    template <typename T>
    struct has_value_type<T, std::void_t<typename T::value_type>> : std::true_type {};
    template <typename T>
    inline constexpr bool has_value_type_v = has_value_type<T>::value;
}

/**
 * @brief Represents a hierarchical key-value dictionary/mapping of configuration nodes.
 * Keys are matched case-insensitively, while preserving insertion order.
 */
class ConfigObjectNode : public ConfigNode {
public:
    using MapType = std::unordered_map<std::string, std::shared_ptr<ConfigNode>, CaseInsensitiveHash, CaseInsensitiveEqual>;

    explicit ConfigObjectNode(ConfigOperator op = ConfigOperator::Assign, SourceInfo source = SourceInfo::unknown()) {
        op_ = op;
        source_ = std::move(source);
    }

    [[nodiscard]] ConfigNodeType node_type() const noexcept override {
        return ConfigNodeType::Object;
    }

    [[nodiscard]] bool is_struct() const noexcept {
        return is_struct_;
    }

    void set_struct(bool is_struct = true) {
        is_struct_ = is_struct;
        for (auto& [key, child] : children_) {
            if (child && child->is_object()) {
                child->as_object()->set_struct(is_struct);
            }
        }
    }

    void set_read_only(bool read_only = true) override {
        ConfigNode::set_read_only(read_only);
        for (auto& [key, child] : children_) {
            if (child) {
                child->set_read_only(read_only);
            }
        }
    }

    [[nodiscard]] bool contains_key(std::string_view key) const noexcept {
        return children_.find(std::string(key)) != children_.end();
    }

    [[nodiscard]] std::shared_ptr<ConfigNode> get_child(std::string_view key) const {
        auto it = children_.find(std::string(key));
        if (it != children_.end()) {
            return it->second;
        }
        return nullptr;
    }

    bool try_get_child(std::string_view key, std::shared_ptr<ConfigNode>& out_node) const {
        auto it = children_.find(std::string(key));
        if (it != children_.end()) {
            out_node = it->second;
            return true;
        }
        out_node = nullptr;
        return false;
    }

    void set_child(std::string key, std::shared_ptr<ConfigNode> node, bool allow_new_key_in_struct = false) {
        ensure_writable();
        auto it = children_.find(key);
        if (is_struct_ && !allow_new_key_in_struct && it == children_.end()) {
            throw ConfigStructException(
                key,
                "Cannot add new key '" + key + "' to a struct-locked configuration node. Use '+key=...' or '++key=...' to explicitly add keys.",
                (node && !node->source().is_unknown()) ? node->source() : source_
            );
        }
        if (it == children_.end()) {
            keys_.push_back(key);
            children_.emplace(std::move(key), std::move(node));
        } else {
            it->second = std::move(node);
        }
    }

    bool remove_child(std::string_view key) {
        ensure_writable();
        auto it = children_.find(std::string(key));
        if (it == children_.end()) {
            return false;
        }
        std::string actual_key = it->first;
        children_.erase(it);
        keys_.erase(std::remove_if(keys_.begin(), keys_.end(),
            [&actual_key](const std::string& k) {
                return CaseInsensitiveEqual{}(k, actual_key);
            }), keys_.end());
        return true;
    }

    [[nodiscard]] const std::vector<std::string>& keys() const noexcept {
        return keys_;
    }

    [[nodiscard]] const MapType& children() const noexcept {
        return children_;
    }

    [[nodiscard]] size_t size() const noexcept {
        return children_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return children_.empty();
    }

    [[nodiscard]] std::shared_ptr<ConfigNode> select_node(std::string_view path) const {
        if (path.empty()) {
            return std::const_pointer_cast<ConfigNode>(shared_from_this());
        }

        std::shared_ptr<ConfigNode> current = std::const_pointer_cast<ConfigNode>(shared_from_this());

        size_t start = 0;
        while (start < path.size()) {
            size_t dot_pos = path.find('.', start);
            std::string_view part = (dot_pos == std::string_view::npos)
                ? path.substr(start)
                : path.substr(start, dot_pos - start);

            start = (dot_pos == std::string_view::npos) ? path.size() : dot_pos + 1;

            if (part.empty()) continue;

            size_t bracket_idx = part.find('[');
            if (bracket_idx != std::string_view::npos && ends_with(part, ']')) {
                std::string_view prop_name = part.substr(0, bracket_idx);
                if (!prop_name.empty()) {
                    if (current->is_object()) {
                        auto child = current->as_object()->get_child(prop_name);
                        if (!child) return nullptr;
                        current = child;
                    } else {
                        return nullptr;
                    }
                }

                std::string_view indices = part.substr(bracket_idx);
                while (starts_with(indices, '[')) {
                    size_t close_idx = indices.find(']');
                    if (close_idx == std::string_view::npos) return nullptr;

                    std::string_view idx_str = indices.substr(1, close_idx - 1);
                    // Trim whitespace
                    while (!idx_str.empty() && std::isspace(static_cast<unsigned char>(idx_str.front()))) idx_str.remove_prefix(1);
                    while (!idx_str.empty() && std::isspace(static_cast<unsigned char>(idx_str.back()))) idx_str.remove_suffix(1);

                    // Try parsing as integer for list
                    bool is_int = !idx_str.empty();
                    size_t sign_offset = 0;
                    if (idx_str.front() == '-' || idx_str.front() == '+') sign_offset = 1;
                    if (sign_offset >= idx_str.size()) is_int = false;
                    for (size_t i = sign_offset; i < idx_str.size(); ++i) {
                        if (!std::isdigit(static_cast<unsigned char>(idx_str[i]))) {
                            is_int = false;
                            break;
                        }
                    }

                    if (is_int && current->is_list()) {
                        int64_t index = std::stoll(std::string(idx_str));
                        auto* list_node = current->as_list();
                        int64_t actual_idx = (index < 0) ? static_cast<int64_t>(list_node->size()) + index : index;
                        if (actual_idx >= 0 && actual_idx < static_cast<int64_t>(list_node->size())) {
                            current = (*list_node)[static_cast<size_t>(actual_idx)];
                        } else {
                            return nullptr;
                        }
                    } else if (current->is_object()) {
                        auto child = current->as_object()->get_child(idx_str);
                        if (!child) return nullptr;
                        current = child;
                    } else {
                        return nullptr;
                    }

                    indices.remove_prefix(close_idx + 1);
                }
            } else if (current->is_list()) {
                // Check if part is a numeric index: servers.0.host
                bool is_num = !part.empty();
                size_t sign_offset = 0;
                if (part.front() == '-' || part.front() == '+') sign_offset = 1;
                if (sign_offset >= part.size()) is_num = false;
                for (size_t i = sign_offset; i < part.size(); ++i) {
                    if (!std::isdigit(static_cast<unsigned char>(part[i]))) {
                        is_num = false;
                        break;
                    }
                }
                if (is_num) {
                    int64_t index = std::stoll(std::string(part));
                    auto* list_node = current->as_list();
                    int64_t actual_idx = (index < 0) ? static_cast<int64_t>(list_node->size()) + index : index;
                    if (actual_idx >= 0 && actual_idx < static_cast<int64_t>(list_node->size())) {
                        current = (*list_node)[static_cast<size_t>(actual_idx)];
                    } else {
                        return nullptr;
                    }
                } else {
                    return nullptr;
                }
            } else if (current->is_object()) {
                auto child = current->as_object()->get_child(part);
                if (!child) return nullptr;
                current = child;
            } else {
                return nullptr;
            }
        }

        return current;
    }

    template <typename T>
    [[nodiscard]] T select(std::string_view path, T default_value = T{}) const {
        auto node = select_node(path);
        if (!node) return default_value;

        if (node->is_value()) {
            const auto* vn = node->as_value();
            if (vn->is_missing_marker() || vn->is_null()) return default_value;

            if constexpr (std::is_same_v<T, std::string>) {
                return vn->to_string();
            } else if constexpr (std::is_same_v<T, bool>) {
                return vn->as_bool();
            } else if constexpr (std::is_integral_v<T>) {
                return static_cast<T>(vn->as_integer());
            } else if constexpr (std::is_floating_point_v<T>) {
                return static_cast<T>(vn->as_double());
            }
        }

        if (node->is_list()) {
            if constexpr (detail::has_value_type_v<T>) {
                const auto* ln = node->as_list();
                T res{};
                using Item = typename T::value_type;
                for (size_t i = 0; i < ln->size(); ++i) {
                    auto item = (*ln)[i];
                    if (item && item->is_value()) {
                        const auto* ivn = item->as_value();
                        if constexpr (std::is_same_v<Item, std::string>) {
                            res.push_back(ivn->to_string());
                        } else if constexpr (std::is_integral_v<Item>) {
                            res.push_back(static_cast<Item>(ivn->as_integer()));
                        } else if constexpr (std::is_floating_point_v<Item>) {
                            res.push_back(static_cast<Item>(ivn->as_double()));
                        } else if constexpr (std::is_same_v<Item, bool>) {
                            res.push_back(ivn->as_bool());
                        }
                    }
                }
                return res;
            }
        }

        return default_value;
    }

    [[nodiscard]] std::string select(std::string_view path, const char* default_value) const {
        return select<std::string>(path, std::string(default_value));
    }

    [[nodiscard]] std::shared_ptr<ConfigNode> clone() const override {
        auto cloned = std::make_shared<ConfigObjectNode>(op_, source_);
        cloned->is_struct_ = is_struct_;
        for (const auto& key : keys_) {
            auto it = children_.find(key);
            if (it != children_.end() && it->second) {
                cloned->set_child(key, it->second->clone(), true);
            }
        }
        return cloned;
    }

    [[nodiscard]] std::string to_string() const override {
        std::ostringstream ss;
        ss << "{";
        for (size_t i = 0; i < keys_.size(); ++i) {
            if (i > 0) ss << ", ";
            const auto& key = keys_[i];
            ss << "'" << key << "': ";
            auto it = children_.find(key);
            if (it != children_.end() && it->second) {
                format_python_node(ss, it->second.get());
            } else {
                ss << "None";
            }
        }
        ss << "}";
        return ss.str();
    }

private:
    std::vector<std::string> keys_{};
    MapType children_{};
    bool is_struct_{false};

    static void format_python_node(std::ostream& os, const ConfigNode* node) {
        if (!node) {
            os << "None";
            return;
        }
        if (node->is_value()) {
            const auto* vn = node->as_value();
            if (vn->is_missing_marker()) {
                os << "'???'";
            } else if (vn->is_null()) {
                os << "None";
            } else if (vn->is_bool()) {
                os << (vn->as_bool() ? "True" : "False");
            } else if (vn->is_string()) {
                os << "'" << vn->as_string() << "'";
            } else {
                os << vn->to_string();
            }
        } else if (node->is_list()) {
            const auto* ln = node->as_list();
            os << "[";
            for (size_t i = 0; i < ln->size(); ++i) {
                if (i > 0) os << ", ";
                format_python_node(os, (*ln)[i].get());
            }
            os << "]";
        } else if (node->is_object()) {
            os << node->to_string();
        }
    }
};

// Inline implementations of downcasts from ConfigNode
inline ConfigValueNode* ConfigNode::as_value() noexcept {
    return is_value() ? static_cast<ConfigValueNode*>(this) : nullptr;
}
inline const ConfigValueNode* ConfigNode::as_value() const noexcept {
    return is_value() ? static_cast<const ConfigValueNode*>(this) : nullptr;
}
inline ConfigListNode* ConfigNode::as_list() noexcept {
    return is_list() ? static_cast<ConfigListNode*>(this) : nullptr;
}
inline const ConfigListNode* ConfigNode::as_list() const noexcept {
    return is_list() ? static_cast<const ConfigListNode*>(this) : nullptr;
}
inline ConfigObjectNode* ConfigNode::as_object() noexcept {
    return is_object() ? static_cast<ConfigObjectNode*>(this) : nullptr;
}
inline const ConfigObjectNode* ConfigNode::as_object() const noexcept {
    return is_object() ? static_cast<const ConfigObjectNode*>(this) : nullptr;
}

} // namespace omegaconf
