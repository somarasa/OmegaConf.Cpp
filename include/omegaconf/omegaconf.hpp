#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <optional>
#include <vector>
#include <unordered_map>

namespace omegaconf {

/// @brief Returns the version string of OmegaConf.Cpp.
[[nodiscard]] std::string_view version() noexcept;

} // namespace omegaconf
