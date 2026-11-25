// nexigen-canview — CiA 306 EDS/DCF parser.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "eds_dictionary.hpp"

namespace nexigen::canview {

/// @brief One parser diagnostic tied to a source line.
struct EdsDiagnostic {
    std::size_t line{0};   ///< 1-based line, or zero for file-level errors.
    std::string message{}; ///< Human-readable reason.

    /// @brief Cold-path rendering.
    /// @return `line N: message`.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Parser limits.
struct EdsLimits {
    std::size_t max_file_bytes{8U * 1024U * 1024U}; ///< Maximum input file size.
    std::size_t max_objects{65'536U};               ///< Maximum object entries.
    std::size_t max_name_bytes{128U};               ///< Maximum parameter name.
    std::size_t max_line_bytes{4'096U};             ///< Maximum source line.
};

/// @brief Parser outcome.
struct EdsParseResult {
    std::unique_ptr<EdsDictionary> dictionary{}; ///< Parsed dictionary, null on failure.
    std::vector<EdsDiagnostic> diagnostics{};    ///< Line diagnostics.

    /// @brief Whether a dictionary was produced.
    /// @return `true` when parsing succeeded.
    [[nodiscard]] bool ok() const noexcept { return dictionary != nullptr; }
};

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
};

} // namespace nexigen::canview
