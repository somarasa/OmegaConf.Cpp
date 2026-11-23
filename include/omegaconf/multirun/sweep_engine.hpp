#pragma once

#include "omegaconf/common/exceptions.hpp"

#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include <regex>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <random>

namespace fs = std::filesystem;

namespace omegaconf {

class SweepEngine {
public:
    [[nodiscard]] static bool is_multirun(const std::vector<std::string>& args) {
        for (const auto& a : args) {
            if (a == "-m" || a == "--multirun") {
                return true;
            }
        }
        return false;
    }

    struct ParseResult {
        std::vector<std::string> non_sweep_args{};
        std::map<std::string, std::vector<std::string>> sweep_matrix{};
        std::vector<std::string> sweep_keys_order{};
    };

    static ParseResult parse_sweep_args(const std::vector<std::string>& args, const std::string& root_dir = "") {
        ParseResult result;
        bool is_multirun_mode = false;
        std::vector<std::string> clean_args;

        for (const auto& arg : args) {
            if (arg == "-m" || arg == "--multirun") {
                is_multirun_mode = true;
            } else {
                clean_args.push_back(arg);
            }
        }

        for (const auto& arg : clean_args) {
            size_t eq_idx = arg.find('=');
            if (is_multirun_mode && eq_idx != std::string::npos && eq_idx > 0) {
                std::string key = std::string(trim(arg.substr(0, eq_idx)));
                std::string raw_values = std::string(trim(arg.substr(eq_idx + 1)));

                std::vector<std::string> func_values;
                if (try_expand_sweep_function(key, raw_values, root_dir, func_values) && !func_values.empty()) {
                    result.sweep_matrix[key] = std::move(func_values);
                    result.sweep_keys_order.push_back(key);
                    continue;
                }

                if (raw_values.find(',') != std::string::npos &&
                    !(starts_with(raw_values, '[') && ends_with(raw_values, ']'))) {
                    auto values = split_top_level_commas(raw_values);
                    if (values.size() > 1) {
                        result.sweep_matrix[key] = std::move(values);
                        result.sweep_keys_order.push_back(key);
                        continue;
                    }
                }
            }

            result.non_sweep_args.push_back(arg);
        }

        return result;
    }

    static std::vector<std::map<std::string, std::string>> generate_cartesian_product(
        const std::map<std::string, std::vector<std::string>>& sweep_matrix,
        const std::vector<std::string>& keys_order = {}) {
        std::vector<std::map<std::string, std::string>> result;
        if (sweep_matrix.empty()) {
            result.emplace_back();
            return result;
        }

        std::vector<std::string> keys;
        if (!keys_order.empty()) {
            keys = keys_order;
        } else {
            keys.reserve(sweep_matrix.size());
            for (const auto& [k, _] : sweep_matrix) {
                keys.push_back(k);
            }
        }

        std::map<std::string, std::string> current;
        generate_combinations(sweep_matrix, keys, 0, current, result);
        return result;
    }

    static std::vector<std::string> split_top_level_commas(std::string_view input) {
        std::vector<std::string> result;
        int depth = 0;
        size_t last_start = 0;

        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];
            if (c == '[' || c == '(' || c == '{') {
                ++depth;
            } else if (c == ']' || c == ')' || c == '}') {
                --depth;
            } else if (c == ',' && depth == 0) {
                std::string part(trim(input.substr(last_start, i - last_start)));
                if (!part.empty()) result.push_back(std::move(part));
                last_start = i + 1;
            }
        }

        std::string tail(trim(input.substr(last_start)));
        if (!tail.empty()) result.push_back(std::move(tail));

        return result;
    }

private:
    static std::string_view trim(std::string_view s) noexcept {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    }

    static bool try_expand_sweep_function(
        const std::string& key,
        const std::string& raw_values,
        const std::string& root_dir,
        std::vector<std::string>& expanded_values) {
        expanded_values.clear();
        if (!ends_with(raw_values, ')')) return false;

        size_t open_paren = raw_values.find('(');
        if (open_paren == std::string::npos || open_paren == 0) return false;

        std::string func_name = std::string(trim(raw_values.substr(0, open_paren)));
        std::transform(func_name.begin(), func_name.end(), func_name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        std::string inner = std::string(trim(raw_values.substr(open_paren + 1, raw_values.size() - open_paren - 2)));

        if (func_name == "choice") {
            expanded_values = split_top_level_commas(inner);
            return true;
        }

        if (func_name == "range") {
            expanded_values = expand_range(inner);
            return true;
        }

        if (func_name == "linspace" || func_name == "interval") {
            expanded_values = expand_linspace(inner);
            return true;
        }

        if (func_name == "glob") {
            expanded_values = expand_glob(key, inner, root_dir);
            return true;
        }

        if (func_name == "sort") {
            expanded_values = expand_sort_sweep(key, inner, root_dir);
            return true;
        }

        if (func_name == "shuffle") {
            expanded_values = expand_shuffle_sweep(key, inner, root_dir);
            return true;
        }

        if (func_name == "tag") {
            auto tag_args = split_top_level_commas(inner);
            if (tag_args.size() < 2) {
                throw ConfigSyntaxException("tag() requires at least one tag and a sweep argument: 'tag(" + inner + ")'.");
            }
            std::string inner_sweep_expr = tag_args.back();
            if (starts_with_case_insensitive(inner_sweep_expr, "sweep=")) {
                inner_sweep_expr = std::string(trim(inner_sweep_expr.substr(6)));
            }
            return try_expand_sweep_function(key, inner_sweep_expr, root_dir, expanded_values);
        }

        if (func_name == "int" || func_name == "float" || func_name == "str" || func_name == "bool") {
            std::vector<std::string> inner_expanded;
            if (try_expand_sweep_function(key, inner, root_dir, inner_expanded) && !inner_expanded.empty()) {
                for (auto& v : inner_expanded) {
                    expanded_values.push_back(func_name + "(" + v + ")");
                }
                return true;
            }
            auto cast_parts = split_top_level_commas(inner);
            if (cast_parts.size() > 1) {
                for (auto& v : cast_parts) {
                    expanded_values.push_back(func_name + "(" + v + ")");
                }
                return true;
            }
            return false;
        }

        return false;
    }

    static bool is_integer(std::string_view s) noexcept {
        s = trim(s);
        if (s.empty()) return false;
        size_t i = 0;
        if (s[0] == '-' || s[0] == '+') i = 1;
        if (i >= s.size()) return false;
        for (; i < s.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        }
        return true;
    }

    static std::vector<std::string> expand_range(const std::string& inner) {
        auto parts = split_top_level_commas(inner);
        if (parts.empty() || parts.size() > 3) {
            throw ConfigSyntaxException("Invalid range() syntax 'range(" + inner + ")'. Expected 1 to 3 numeric arguments.");
        }

        bool all_int = true;
        for (const auto& p : parts) {
            if (!is_integer(p)) {
                all_int = false;
                break;
            }
        }

        if (all_int) {
            int64_t start = parts.size() == 1 ? 0 : std::stoll(parts[0]);
            int64_t stop = parts.size() == 1 ? std::stoll(parts[0]) : std::stoll(parts[1]);
            int64_t step = parts.size() == 3 ? std::stoll(parts[2]) : (start <= stop ? 1 : -1);

            if (step == 0) {
                throw ConfigSyntaxException("range() step argument cannot be zero.");
            }

            std::vector<std::string> results;
            if (step > 0) {
                for (int64_t v = start; v < stop; v += step) {
                    results.push_back(std::to_string(v));
                }
            } else {
                for (int64_t v = start; v > stop; v += step) {
                    results.push_back(std::to_string(v));
                }
            }
            return results;
        } else {
            double start = parts.size() == 1 ? 0.0 : std::stod(parts[0]);
            double stop = parts.size() == 1 ? std::stod(parts[0]) : std::stod(parts[1]);
            double step = parts.size() == 3 ? std::stod(parts[2]) : (start <= stop ? 1.0 : -1.0);

            if (std::abs(step) < 1e-15) {
                throw ConfigSyntaxException("range() step argument cannot be zero.");
            }

            std::vector<std::string> results;
            if (step > 0) {
                for (double v = start; v < stop - 1e-12; v += step) {
                    results.push_back(format_float(v));
                }
            } else {
                for (double v = start; v > stop + 1e-12; v += step) {
                    results.push_back(format_float(v));
                }
            }
            return results;
        }
    }

    static std::string format_float(double v) {
        std::ostringstream ss;
        ss << std::setprecision(10) << v;
        std::string s = ss.str();
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
            s += ".0";
        }
        return s;
    }

    static std::vector<std::string> expand_linspace(const std::string& inner) {
        auto parts = split_top_level_commas(inner);
        if (parts.size() < 2 || parts.size() > 3) {
            throw ConfigSyntaxException("Invalid linspace/interval syntax '(" + inner + ")'. Expected (start, end[, count]).");
        }

        double start = std::stod(parts[0]);
        double end = std::stod(parts[1]);
        int count = parts.size() == 3 ? std::stoi(parts[2]) : 5;

        if (count <= 0) {
            throw ConfigSyntaxException("linspace/interval count must be greater than zero.");
        }

        std::vector<std::string> results;
        results.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            double val = count == 1 ? start : start + (end - start) * i / (count - 1);
            results.push_back(format_float(val));
        }

        return results;
    }

    static std::vector<std::string> expand_sort_sweep(const std::string& key, const std::string& inner, const std::string& root_dir) {
        auto parts = split_top_level_commas(inner);
        bool reverse = false;

        if (!parts.empty()) {
            std::string last = parts.back();
            last.erase(std::remove_if(last.begin(), last.end(), ::isspace), last.end());
            to_lower_in_place(last);
            if (last == "reverse=true") {
                reverse = true;
                parts.pop_back();
            } else if (last == "reverse=false") {
                parts.pop_back();
            }
        }

        if (parts.size() == 1) {
            std::string single = parts[0];
            if (starts_with_case_insensitive(single, "sweep=")) {
                single = std::string(trim(single.substr(6)));
            }
            std::vector<std::string> nested;
            if (try_expand_sweep_function(key, single, root_dir, nested) && !nested.empty()) {
                parts = std::move(nested);
            }
        }

        bool all_numeric = true;
        for (const auto& p : parts) {
            try {
                size_t idx = 0;
                (void)std::stod(p, &idx);
                if (idx != p.size()) all_numeric = false;
            } catch (...) {
                all_numeric = false;
                break;
            }
        }

        if (all_numeric) {
            std::sort(parts.begin(), parts.end(), [](const std::string& a, const std::string& b) {
                return std::stod(a) < std::stod(b);
            });
        } else {
            std::sort(parts.begin(), parts.end());
        }

        if (reverse) {
            std::reverse(parts.begin(), parts.end());
        }
        return parts;
    }

    static std::vector<std::string> expand_shuffle_sweep(const std::string& key, std::string inner, const std::string& root_dir) {
        if (starts_with_case_insensitive(inner, "sweep=")) {
            inner = std::string(trim(inner.substr(6)));
        }
        std::vector<std::string> parts;
        if (try_expand_sweep_function(key, inner, root_dir, parts) && !parts.empty()) {
            // parts filled
        } else {
            parts = split_top_level_commas(inner);
        }

        static std::mt19937 g{std::random_device{}()};
        std::shuffle(parts.begin(), parts.end(), g);
        return parts;
    }

    static std::vector<std::string> expand_glob(const std::string& raw_key, const std::string& inner, const std::string& root_dir) {
        std::string clean_key = raw_key;
        while (!clean_key.empty() && clean_key.front() == '+') clean_key.erase(clean_key.begin());
        size_t at_idx = clean_key.find('@');
        std::string group_folder = at_idx != std::string::npos ? clean_key.substr(0, at_idx) : clean_key;

        fs::path base_root = root_dir.empty() ? fs::current_path() : fs::path(root_dir);
        fs::path group_dir = base_root / group_folder;

        if (!fs::exists(group_dir) || !fs::is_directory(group_dir)) {
            return {};
        }

        auto args = split_top_level_commas(inner);
        std::vector<std::string> include_patterns;
        std::vector<std::string> exclude_patterns;

        for (const auto& raw_arg : args) {
            std::string arg = std::string(trim(raw_arg));
            if (starts_with_case_insensitive(arg, "exclude=")) {
                std::string ex_val = std::string(trim(arg.substr(8)));
                for (auto& p : parse_glob_pattern_list(ex_val)) exclude_patterns.push_back(std::move(p));
            } else if (starts_with_case_insensitive(arg, "include=")) {
                std::string inc_val = std::string(trim(arg.substr(8)));
                for (auto& p : parse_glob_pattern_list(inc_val)) include_patterns.push_back(std::move(p));
            } else {
                for (auto& p : parse_glob_pattern_list(arg)) include_patterns.push_back(std::move(p));
            }
        }

        if (include_patterns.empty()) include_patterns.push_back("*");

        std::vector<std::regex> inc_regexes;
        for (const auto& p : include_patterns) inc_regexes.push_back(wildcard_to_regex(p));
        std::vector<std::regex> exc_regexes;
        for (const auto& p : exclude_patterns) exc_regexes.push_back(wildcard_to_regex(p));

        static const std::set<std::string> supported_exts = {".yaml", ".yml", ".json", ".jsonc", ".toml"};
        std::set<std::string> matched_names;

        for (const auto& entry : fs::directory_iterator(group_dir)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            to_lower_in_place(ext);
            if (supported_exts.find(ext) == supported_exts.end()) continue;

            std::string stem = entry.path().stem().string();

            bool matches_inc = false;
            for (const auto& r : inc_regexes) {
                if (std::regex_match(stem, r)) {
                    matches_inc = true;
                    break;
                }
            }
            if (!matches_inc) continue;

            bool matches_exc = false;
            for (const auto& r : exc_regexes) {
                if (std::regex_match(stem, r)) {
                    matches_exc = true;
                    break;
                }
            }
            if (!matches_exc) {
                matched_names.insert(stem);
            }
        }

        return std::vector<std::string>(matched_names.begin(), matched_names.end());
    }

    static std::vector<std::string> parse_glob_pattern_list(const std::string& raw) {
        if (starts_with(raw, '[') && ends_with(raw, ']')) {
            auto items = split_top_level_commas(raw.substr(1, raw.size() - 2));
            std::vector<std::string> cleaned;
            for (auto& item : items) {
                std::string s = std::string(trim(item));
                while (!s.empty() && (s.front() == '\'' || s.front() == '"')) s.erase(s.begin());
                while (!s.empty() && (s.back() == '\'' || s.back() == '"')) s.pop_back();
                if (!s.empty()) cleaned.push_back(s);
            }
            return cleaned;
        }

        std::string s = std::string(trim(raw));
        while (!s.empty() && (s.front() == '\'' || s.front() == '"')) s.erase(s.begin());
        while (!s.empty() && (s.back() == '\'' || s.back() == '"')) s.pop_back();
        if (!s.empty()) return {s};
        return {};
    }

    static std::regex wildcard_to_regex(const std::string& pattern) {
        std::string rx = "^";
        for (char c : pattern) {
            if (c == '*') rx += ".*";
            else if (c == '?') rx += ".";
            else if (c == '.' || c == '+' || c == '(' || c == ')' || c == '[' || c == ']' ||
                     c == '{' || c == '}' || c == '^' || c == '$' || c == '|' || c == '\\') {
                rx += '\\';
                rx += c;
            } else {
                rx += c;
            }
        }
        rx += "$";
        return std::regex(rx, std::regex_constants::icase);
    }

    static bool starts_with_case_insensitive(std::string_view str, std::string_view prefix) noexcept {
        if (str.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(str[i])) !=
                std::tolower(static_cast<unsigned char>(prefix[i]))) {
                return false;
            }
        }
        return true;
    }

    static void generate_combinations(
        const std::map<std::string, std::vector<std::string>>& matrix,
        const std::vector<std::string>& keys,
        size_t key_index,
        std::map<std::string, std::string>& current,
        std::vector<std::map<std::string, std::string>>& result) {
        if (key_index >= keys.size()) {
            result.push_back(current);
            return;
        }

        const std::string& current_key = keys[key_index];
        auto it = matrix.find(current_key);
        if (it != matrix.end()) {
            for (const auto& val : it->second) {
                current[current_key] = val;
                generate_combinations(matrix, keys, key_index + 1, current, result);
                current.erase(current_key);
            }
        }
    }
};

} // namespace omegaconf
