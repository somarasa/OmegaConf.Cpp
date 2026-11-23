#pragma once

#include <string_view>
#include <string>
#include <cctype>

namespace omegaconf {
namespace string_utils {

inline bool starts_with(std::string_view sv, std::string_view prefix) noexcept {
    return sv.size() >= prefix.size() && sv.compare(0, prefix.size(), prefix) == 0;
}

inline bool starts_with(std::string_view sv, char c) noexcept {
    return !sv.empty() && sv.front() == c;
}

inline bool ends_with(std::string_view sv, std::string_view suffix) noexcept {
    return sv.size() >= suffix.size() && sv.compare(sv.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool ends_with(std::string_view sv, char c) noexcept {
    return !sv.empty() && sv.back() == c;
}

inline std::string_view trim_left(std::string_view sv) noexcept {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    return sv;
}

inline std::string_view trim_right(std::string_view sv) noexcept {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return sv;
}

inline std::string_view trim(std::string_view sv) noexcept {
    return trim_right(trim_left(sv));
}

inline void to_lower_in_place(std::string& s) noexcept {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
}

inline std::string to_lower(std::string_view sv) {
    std::string result(sv);
    to_lower_in_place(result);
    return result;
}

} // namespace string_utils

using string_utils::starts_with;
using string_utils::ends_with;
using string_utils::to_lower_in_place;
using string_utils::to_lower;

} // namespace omegaconf
