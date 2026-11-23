#pragma once

#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include "omegaconf/ast/config_list_node.hpp"
#include "omegaconf/ast/config_object_node.hpp"

#include <string>
#include <sstream>
#include <string_view>
#include <iomanip>
#include <cctype>

namespace omegaconf {

class YamlSerializer {
public:
    static std::string serialize(const ConfigNode* node, int indent = 0) {
        if (!node) return "null\n";

        if (node->is_value()) {
            return serialize_value(node->as_value()) + "\n";
        }

        if (node->is_list()) {
            std::ostringstream ss;
            serialize_list(node->as_list(), indent, ss);
            return ss.str();
        }

        if (node->is_object()) {
            std::ostringstream ss;
            serialize_object(node->as_object(), indent, ss);
            return ss.str();
        }

        return "null\n";
    }

private:
    static std::string serialize_value(const ConfigValueNode* vn) {
        if (!vn || vn->is_null()) return "null";
        if (vn->is_missing_marker()) return "???";
        if (vn->is_bool()) return vn->as_bool() ? "true" : "false";
        if (vn->is_integer()) return std::to_string(vn->as_integer());
        if (vn->is_double()) {
            std::ostringstream ss;
            ss << vn->as_double();
            return ss.str();
        }
        if (vn->is_string()) {
            return format_string(vn->as_string());
        }
        return "null";
    }

    static std::string format_string(std::string_view s) {
        if (s.empty()) return "\"\"";
        if (s == "???" || s == "null" || s == "true" || s == "false" || s == "yes" || s == "no" || s == "on" || s == "off") {
            return "\"" + std::string(s) + "\"";
        }

        bool needs_quotes = false;
        if (std::isdigit(static_cast<unsigned char>(s[0])) || s[0] == '-' || s[0] == '+' || s[0] == '@' || s[0] == '[' || s[0] == '{') {
            needs_quotes = true;
        }

        for (char c : s) {
            if (c == ':' || c == '#' || c == '\'' || c == '"' || c == '\n' || c == '\r' || c == '\t' || c == '{' || c == '}' || c == '[' || c == ']' || c == ',') {
                needs_quotes = true;
                break;
            }
        }

        if (!needs_quotes) {
            return std::string(s);
        }

        std::ostringstream ss;
        ss << '"';
        for (char c : s) {
            if (c == '"') ss << "\\\"";
            else if (c == '\\') ss << "\\\\";
            else if (c == '\n') ss << "\\n";
            else if (c == '\r') ss << "\\r";
            else if (c == '\t') ss << "\\t";
            else ss << c;
        }
        ss << '"';
        return ss.str();
    }

    static void serialize_list(const ConfigListNode* ln, int indent, std::ostringstream& ss) {
        if (!ln || ln->size() == 0) {
            ss << std::string(static_cast<size_t>(indent), ' ') << "[]\n";
            return;
        }

        std::string pad(static_cast<size_t>(indent), ' ');
        for (size_t i = 0; i < ln->size(); ++i) {
            const auto& item = (*ln)[i];
            if (!item) {
                ss << pad << "- null\n";
            } else if (item->is_value()) {
                ss << pad << "- " << serialize_value(item->as_value()) << "\n";
            } else if (item->is_list()) {
                ss << pad << "-\n";
                serialize_list(item->as_list(), indent + 2, ss);
            } else if (item->is_object()) {
                const auto* obj = item->as_object();
                const auto& keys = obj->keys();
                if (keys.empty()) {
                    ss << pad << "- {}\n";
                } else {
                    auto first_child = obj->get_child(keys[0]);
                    if (first_child && first_child->is_value()) {
                        ss << pad << "- " << keys[0] << ": " << serialize_value(first_child->as_value()) << "\n";
                        for (size_t k = 1; k < keys.size(); ++k) {
                            auto c = obj->get_child(keys[k]);
                            if (c && c->is_value()) {
                                ss << pad << "  " << keys[k] << ": " << serialize_value(c->as_value()) << "\n";
                            } else if (c && c->is_object()) {
                                ss << pad << "  " << keys[k] << ":\n";
                                serialize_object(c->as_object(), indent + 4, ss);
                            } else if (c && c->is_list()) {
                                ss << pad << "  " << keys[k] << ":\n";
                                serialize_list(c->as_list(), indent + 4, ss);
                            }
                        }
                    } else {
                        ss << pad << "-\n";
                        serialize_object(obj, indent + 2, ss);
                    }
                }
            }
        }
    }

    static void serialize_object(const ConfigObjectNode* on, int indent, std::ostringstream& ss) {
        if (!on || on->keys().empty()) {
            if (indent == 0) {
                ss << "{}\n";
            } else {
                ss << std::string(static_cast<size_t>(indent), ' ') << "{}\n";
            }
            return;
        }

        std::string pad(static_cast<size_t>(indent), ' ');
        const auto& keys = on->keys();
        for (const auto& key : keys) {
            auto child = on->get_child(key);
            if (!child) {
                ss << pad << key << ": null\n";
            } else if (child->is_value()) {
                ss << pad << key << ": " << serialize_value(child->as_value()) << "\n";
            } else if (child->is_object()) {
                const auto* obj = child->as_object();
                if (obj->keys().empty()) {
                    ss << pad << key << ": {}\n";
                } else {
                    ss << pad << key << ":\n";
                    serialize_object(obj, indent + 2, ss);
                }
            } else if (child->is_list()) {
                const auto* lst = child->as_list();
                if (lst->size() == 0) {
                    ss << pad << key << ": []\n";
                } else {
                    ss << pad << key << ":\n";
                    serialize_list(lst, indent + 2, ss);
                }
            }
        }
    }
};

} // namespace omegaconf
