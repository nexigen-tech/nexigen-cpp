// nexigen::canopen — CiA 306 EDS/DCF parser.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nexigen/canopen/eds_limits.hpp>
#include <nexigen/canopen/eds_parse_result.hpp>

namespace nexigen::canopen {

/// @brief FNV-1a 64-bit content hash.
/// @param content Bytes to hash.
/// @return Hash value.
[[nodiscard]] std::uint64_t eds_content_hash(std::string_view content) noexcept;

/// @brief Parser for EDS/DCF object dictionaries.
class EdsParser {
public:
    /// @brief Parse in-memory EDS text.
    /// @param content File contents.
    /// @param source Source name recorded in diagnostics.
    /// @param limits Size/line/object limits.
    /// @return Dictionary or diagnostics.
    [[nodiscard]] static EdsParseResult parse(std::string_view content, std::string_view source,
                                              const EdsLimits& limits = {});

    /// @brief Read and parse a file.
    /// @param path File path.
    /// @param limits Size/line/object limits.
    /// @return Dictionary or diagnostics.
    [[nodiscard]] static EdsParseResult load(const std::string& path, const EdsLimits& limits = {});

    /// @brief Cold-path diagnostic rendering.
    /// @return Class diagnostic string.
    [[nodiscard]] std::string to_string() const { return "EdsParser"; }
};

} // namespace nexigen::canopen
