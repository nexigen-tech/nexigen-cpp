// nexigen::canopen — CiA 306 parser limits.
#pragma once

#include <cstddef>
#include <format>
#include <string>

namespace nexigen::canopen {

/// @brief Parser limits.
struct EdsLimits {
    std::size_t max_file_bytes{8U * 1024U * 1024U}; ///< Maximum input file size.
    std::size_t max_objects{65'536U};               ///< Maximum object entries.
    std::size_t max_name_bytes{128U};               ///< Maximum parameter name.
    std::size_t max_line_bytes{4'096U};             ///< Maximum source line.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line limits summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("EdsLimits(file={}, objects={}, name={}, line={})", max_file_bytes,
                           max_objects, max_name_bytes, max_line_bytes);
    }
};

/// @brief Free helper mirroring `EdsLimits::to_string`.
[[nodiscard]] inline std::string to_string(const EdsLimits& limits) {
    return limits.to_string();
}

} // namespace nexigen::canopen
