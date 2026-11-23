#pragma once

#include "omegaconf/common/config.hpp"
#include "omegaconf/common/string_utils.hpp"

#include <string>
#include <string_view>
#include <sstream>

namespace omegaconf {

/**
 * @brief Tracks the source origin (file path, line number, and column number)
 * of a configuration node for error reporting and diagnostics.
 */
struct SourceInfo {
    std::string file_path{};
    int64_t line{0};
    int64_t column{0};

    SourceInfo() = default;
    SourceInfo(std::string path, int64_t l, int64_t c)
        : file_path(std::move(path)), line(l), column(c) {}

    [[nodiscard]] static SourceInfo unknown() noexcept {
        return SourceInfo{};
    }

    [[nodiscard]] bool is_unknown() const noexcept {
        return file_path.empty() && line == 0 && column == 0;
    }

    [[nodiscard]] bool operator==(const SourceInfo& other) const noexcept {
        return file_path == other.file_path && line == other.line && column == other.column;
    }

    [[nodiscard]] bool operator!=(const SourceInfo& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] std::string to_string() const {
        if (file_path.empty()) {
            if (line > 0) {
                return "Line " + std::to_string(line) + ", Col " + std::to_string(column);
            }
            return "(dynamic/cli)";
        }
        if (line > 0) {
            return file_path + ":" + std::to_string(line) + ":" + std::to_string(column);
        }
        return file_path;
    }
};

} // namespace omegaconf
