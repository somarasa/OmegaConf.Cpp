#pragma once

#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include "omegaconf/ast/config_list_node.hpp"
#include "omegaconf/ast/config_object_node.hpp"

#include <string>
#include <sstream>
#include <iomanip>

namespace omegaconf {

class JsonSerializer {
public:
    static std::string serialize(const ConfigNode* node, bool indented = false, int indent_level = 0) {
        if (!node) return "null";

        if (node->is_value()) {
            const auto* vn = node->as_value();
            if (vn->is_missing_marker() || vn->is_null()) {
                return "null";
            }
            if (vn->is_bool()) {
                return vn->as_bool() ? "true" : "false";
            }
            if (vn->is_integer()) {
                return std::to_string(vn->as_integer());
            }
            if (vn->is_double()) {
                std::ostringstream ss;
                ss << vn->as_double();
                return ss.str();
            }
            if (vn->is_string()) {
                return escape_string(vn->as_string());
            }
            return "null";
        }

        if (node->is_list()) {
            const auto* ln = node->as_list();
            if (ln->size() == 0) return "[]";

            std::ostringstream ss;
            if (indented) {
                ss << "[\n";
                for (size_t i = 0; i < ln->size(); ++i) {
                    ss << std::string(static_cast<size_t>(indent_level + 1) * 2, ' ');
                    ss << serialize((*ln)[i].get(), true, indent_level + 1);
                    if (i + 1 < ln->size()) ss << ",";
                    ss << "\n";
                }
                ss << std::string(static_cast<size_t>(indent_level) * 2, ' ') << "]";
            } else {
                ss << "[";
                for (size_t i = 0; i < ln->size(); ++i) {
                    if (i > 0) ss << ",";
                    ss << serialize((*ln)[i].get(), false, 0);
                }
                ss << "]";
            }
            return ss.str();
        }

        if (node->is_object()) {
            const auto* on = node->as_object();
            const auto& keys = on->keys();
            if (keys.empty()) return "{}";

            std::ostringstream ss;
            if (indented) {
                ss << "{\n";
                for (size_t i = 0; i < keys.size(); ++i) {
                    const auto& key = keys[i];
                    ss << std::string(static_cast<size_t>(indent_level + 1) * 2, ' ');
                    ss << escape_string(key) << ": ";
                    auto child = on->get_child(key);
                    ss << serialize(child.get(), true, indent_level + 1);
                    if (i + 1 < keys.size()) ss << ",";
                    ss << "\n";
                }
                ss << std::string(static_cast<size_t>(indent_level) * 2, ' ') << "}";
            } else {
                ss << "{";
                for (size_t i = 0; i < keys.size(); ++i) {
                    if (i > 0) ss << ",";
                    const auto& key = keys[i];
                    ss << escape_string(key) << ":";
                    auto child = on->get_child(key);
                    ss << serialize(child.get(), false, 0);
                }
                ss << "}";
            }
            return ss.str();
        }

        return "null";
    }

private:
    static std::string escape_string(std::string_view s) {
        std::ostringstream ss;
        ss << '"';
        for (char c : s) {
            switch (c) {
                case '"': ss << "\\\""; break;
                case '\\': ss << "\\\\"; break;
                case '\b': ss << "\\b"; break;
                case '\f': ss << "\\f"; break;
                case '\n': ss << "\\n"; break;
                case '\r': ss << "\\r"; break;
                case '\t': ss << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    } else {
                        ss << c;
                    }
                    break;
            }
        }
        ss << '"';
        return ss.str();
    }
};

} // namespace omegaconf
