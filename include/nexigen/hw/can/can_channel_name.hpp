#pragma once

#include <cstddef>
#include <string_view>

#include <nexigen/core/result.hpp>

namespace nexigen::hw::can {

/// @brief Validate a CAN channel name.
/// @param name Candidate name.
/// @return OK when the name is 1..32 characters from `[a-z0-9_-]`.
[[nodiscard]] core::Status validate_channel_name(std::string_view name) noexcept;

/// @brief Whether a channel name satisfies the service-name rules.
/// @param name Candidate name.
/// @return `true` when `validate_channel_name` returns OK.
[[nodiscard]] bool is_valid_channel_name(std::string_view name) noexcept;

} // namespace nexigen::hw::can
