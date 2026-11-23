#pragma once

#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include "omegaconf/ast/config_list_node.hpp"
#include "omegaconf/ast/config_object_node.hpp"
#include "omegaconf/operators/operator_parser.hpp"
#include "omegaconf/common/exceptions.hpp"
#include "omegaconf/common/source_info.hpp"

#include <ryml.hpp>
#include <string>
#include <string_view>
#include <memory>
#include <algorithm>
#include <cctype>

namespace omegaconf {

namespace detail {
    struct CallbacksGuard {
        ryml::Callbacks prev;
        explicit CallbacksGuard(ryml::Callbacks cb) : prev(ryml::get_callbacks()) {
            ryml::set_callbacks(cb);
        }
        ~CallbacksGuard() {
            ryml::set_callbacks(prev);
        }
    };
} // namespace detail

/**
 * @brief Parses YAML and JSON configuration files into an OmegaConf ConfigObjectNode AST,
 * extracting key-suffix mutation operators (+=, *=, etc.).
 */
class YamlFormatParser {
public:
    static bool can_parse(std::string_view extension) noexcept {
        return extension == ".yaml" || extension == ".yml" ||
               extension == ".json" || extension == ".jsonc" || extension == ".toml";
    }

    static std::shared_ptr<ConfigObjectNode> parse(std::string_view content, std::string_view file_path = "") {
        if (content.empty() || std::all_of(content.begin(), content.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); })) {
            return std::make_shared<ConfigObjectNode>(ConfigOperator::Assign, SourceInfo{std::string(file_path), 0, 0});
        }

        std::string preprocessed = ends_with(file_path, ".toml") ? preprocess_toml(content) : preprocess_jsonc(content);
        std::string path_str(file_path);
        auto prev = ryml::get_callbacks();

        ryml::Callbacks callbacks(&path_str, prev.m_allocate, prev.m_free, [](const char* msg, size_t len, ryml::Location loc, void* user_data) {
            auto* path_ptr = static_cast<std::string*>(user_data);
            SourceInfo src{path_ptr ? *path_ptr : "", static_cast<int64_t>(loc.line), static_cast<int64_t>(loc.col)};
            throw ConfigSyntaxException(std::string(msg, len), src);
        });

        detail::CallbacksGuard guard(callbacks);

        ryml::Tree tree;
        try {
            c4::csubstr src(preprocessed.data(), preprocessed.size());
            c4::csubstr fname(path_str.data(), path_str.size());
            tree = ryml::parse_in_arena(fname, src);
        } catch (const ConfigSyntaxException&) {
            throw;
        } catch (const std::exception& ex) {
            throw ConfigSyntaxException(ex.what(), SourceInfo{path_str, 0, 0});
        }

        if (tree.empty() || tree.root_id() == ryml::NONE) {
            return std::make_shared<ConfigObjectNode>(ConfigOperator::Assign, SourceInfo{path_str, 0, 0});
        }

        if (!tree.is_map(tree.root_id())) {
            throw ConfigSyntaxException("Root of YAML/JSON configuration must be a mapping / object.",
                                        SourceInfo{path_str, 1, 1});
        }

        return convert_mapping(tree, tree.root_id(), path_str, ConfigOperator::Assign);
    }

private:
    static std::string preprocess_toml(std::string_view input) {
        std::string input_str(input);
        std::istringstream iss(input_str);
        std::string line;
        std::string out;
        std::string current_section;

        while (std::getline(iss, line)) {
            std::string_view trimmed(line);
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.remove_prefix(1);
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.remove_suffix(1);

            if (trimmed.empty() || trimmed.front() == '#') {
                out += line;
                out += "\n";
                continue;
            }

            if (trimmed.front() == '[' && trimmed.back() == ']') {
                trimmed.remove_prefix(1);
                trimmed.remove_suffix(1);
                current_section = std::string(trimmed);
                out += current_section + ":\n";
                continue;
            }

            size_t eq = trimmed.find('=');
            if (eq != std::string_view::npos) {
                std::string_view key = trimmed.substr(0, eq);
                std::string_view val = trimmed.substr(eq + 1);
                while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.remove_suffix(1);
                while (!val.empty() && std::isspace(static_cast<unsigned char>(val.front()))) val.remove_prefix(1);

                if (!current_section.empty()) {
                    out += "  ";
                }
                out += std::string(key) + ": " + std::string(val) + "\n";
            } else {
                out += line;
                out += "\n";
            }
        }
        return out;
    }

    static std::string preprocess_jsonc(std::string_view input) {
        std::string out;
        out.reserve(input.size());

        bool in_string = false;
        bool in_line_comment = false;
        bool in_block_comment = false;
        bool escape = false;

        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];

            if (in_line_comment) {
                if (c == '\n') {
                    in_line_comment = false;
                    out.push_back('\n');
                } else {
                    out.push_back(' ');
                }
                continue;
            }

            if (in_block_comment) {
                if (c == '*' && i + 1 < input.size() && input[i + 1] == '/') {
                    in_block_comment = false;
                    out.push_back(' ');
                    out.push_back(' ');
                    ++i;
                } else {
                    out.push_back(c == '\n' ? '\n' : ' ');
                }
                continue;
            }

            if (in_string) {
                out.push_back(c);
                if (escape) {
                    escape = false;
                } else if (c == '\\') {
                    escape = true;
                } else if (c == '"') {
                    in_string = false;
                }
                continue;
            }

            if (c == '"') {
                in_string = true;
                out.push_back(c);
                continue;
            }

            if (c == '/' && i + 1 < input.size()) {
                if (input[i + 1] == '/') {
                    in_line_comment = true;
                    out.push_back(' ');
                    out.push_back(' ');
                    ++i;
                    continue;
                } else if (input[i + 1] == '*') {
                    in_block_comment = true;
                    out.push_back(' ');
                    out.push_back(' ');
                    ++i;
                    continue;
                }
            }

            if (c == ',') {
                size_t j = i + 1;
                while (j < input.size() && std::isspace(static_cast<unsigned char>(input[j]))) {
                    ++j;
                }
                if (j < input.size() && (input[j] == '}' || input[j] == ']')) {
                    out.push_back(' ');
                    continue;
                }
            }

            out.push_back(c);
        }

        return out;
    }
    static std::shared_ptr<ConfigObjectNode> convert_mapping(const ryml::Tree& tree,
                                                            ryml::id_type node_id,
                                                            const std::string& file_path,
                                                            ConfigOperator op) {
        SourceInfo source{file_path, 0, 0};
        auto obj_node = std::make_shared<ConfigObjectNode>(op, source);

        for (ryml::id_type child_id = tree.first_child(node_id); child_id != ryml::NONE; child_id = tree.next_sibling(child_id)) {
            if (!tree.has_key(child_id)) continue;

            auto ryml_key = tree.key(child_id);
            std::string_view raw_key(ryml_key.str, ryml_key.len);

            auto [clean_key, key_op] = OperatorParser::parse_key(raw_key);
            auto child_node = convert_node(tree, child_id, file_path, key_op);
            obj_node->set_child(clean_key, child_node);
        }

        return obj_node;
    }

    static std::shared_ptr<ConfigNode> convert_node(const ryml::Tree& tree,
                                                   ryml::id_type node_id,
                                                   const std::string& file_path,
                                                   ConfigOperator op) {
        SourceInfo source{file_path, 0, 0};

        if (tree.is_map(node_id)) {
            return convert_mapping(tree, node_id, file_path, op);
        }

        if (tree.is_seq(node_id)) {
            auto list_node = std::make_shared<ConfigListNode>(op, source);
            for (ryml::id_type child_id = tree.first_child(node_id); child_id != ryml::NONE; child_id = tree.next_sibling(child_id)) {
                list_node->add_item(convert_node(tree, child_id, file_path, ConfigOperator::Assign));
            }
            return list_node;
        }

        if (tree.has_val(node_id)) {
            return convert_scalar(tree, node_id, source, op);
        }

        return std::make_shared<ConfigValueNode>(nullptr, op, source);
    }

    static std::shared_ptr<ConfigValueNode> convert_scalar(const ryml::Tree& tree,
                                                          ryml::id_type node_id,
                                                          const SourceInfo& source,
                                                          ConfigOperator op) {
        auto val = tree.val(node_id);
        std::string_view raw(val.str, val.len);
        bool is_quoted = tree.is_val_quoted(node_id);

        if (!is_quoted && (val.empty() || raw == "null" || raw == "~")) {
            return std::make_shared<ConfigValueNode>(nullptr, op, source);
        }

        if (raw == ConfigValueNode::MissingMarker) {
            return std::make_shared<ConfigValueNode>(std::string(ConfigValueNode::MissingMarker), op, source);
        }

        if (is_quoted) {
            return std::make_shared<ConfigValueNode>(std::string(raw), op, source);
        }

        // Try boolean
        std::string lower(raw);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (lower == "true" || lower == "yes" || lower == "on") {
            return std::make_shared<ConfigValueNode>(true, op, source);
        }
        if (lower == "false" || lower == "no" || lower == "off") {
            return std::make_shared<ConfigValueNode>(false, op, source);
        }

        // Try integer
        try {
            size_t idx = 0;
            int64_t int_val = std::stoll(std::string(raw), &idx, 0);
            if (idx == raw.size()) {
                return std::make_shared<ConfigValueNode>(int_val, op, source);
            }
        } catch (...) {}

        // Try double (when decimal point or scientific notation is present)
        if (raw.find('.') != std::string_view::npos ||
            raw.find('e') != std::string_view::npos ||
            raw.find('E') != std::string_view::npos) {
            try {
                size_t idx = 0;
                double d_val = std::stod(std::string(raw), &idx);
                if (idx == raw.size()) {
                    return std::make_shared<ConfigValueNode>(d_val, op, source);
                }
            } catch (...) {}
        }

        return std::make_shared<ConfigValueNode>(std::string(raw), op, source);
    }
};

} // namespace omegaconf
