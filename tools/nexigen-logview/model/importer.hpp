// nexigen-logview — bounded import of viewer, daemon, and plain-text logs.
#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/core/result.hpp>

#include "record_ring.hpp"

namespace nexigen::logview {

/// @brief Outcome of one import.
struct ImportStats {
    std::uint64_t imported{0};                   ///< Records added to the history.
    std::uint64_t malformed{0};                  ///< Lines skipped as invalid.
    std::uint64_t plain{0};                      ///< Plain-text lines imported.
    bool mixed_boot{false};                      ///< More than one boot token was present.
    std::string first_boot{};                    ///< First non-empty boot token.
    std::optional<std::int64_t> utc_offset_ns{}; ///< Derived UTC-minus-monotonic offset.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the counters and boot warning.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic mirror of `ImportStats::to_string`.
/// @param stats Stats to render.
/// @return The same text.
[[nodiscard]] inline std::string to_string(const ImportStats& stats) {
    return stats.to_string();
}

/// @brief Import a JSONL or plain-text file into the ring.
///
/// The reader processes one line at a time, so memory stays bounded by the
/// longest line. Viewer JSONL, `nexigen-logd` JSONL, and plain text are
/// accepted; a malformed line is counted and skipped. A partial trailing line
/// without a newline is imported once at end of file.
///
/// @param path File to read.
/// @param ring Destination history.
/// @param stats Import counters.
/// @return `Status{}` on success; `SystemError` when the file cannot be opened.
[[nodiscard]] nexigen::core::Status import_file(const std::string& path, RecordRing& ring,
                                                ImportStats& stats) noexcept;

/// @brief Import one already-materialised line (test and fallback seam).
/// @param line One line without the trailing newline.
/// @param ring Destination history.
/// @param stats Import counters.
/// @return `true` when the line produced a record.
bool import_line(std::string_view line, RecordRing& ring, ImportStats& stats) noexcept;

/// @brief Chunked import over one file.
///
/// The UI opens a session and calls `step()` once per frame; the session reads a
/// bounded number of lines so a large file never blocks a frame. Imports reuse
/// the same line parser as `import_file`.
class ImportSession {
public:
    /// @brief Open a file and record its size for progress.
    /// @param path File to read.
    /// @return The session, or `SystemError` when the file cannot be opened.
    [[nodiscard]] static nexigen::core::Result<std::unique_ptr<ImportSession>>
    open(const std::string& path);

    /// @brief Import one bounded chunk.
    /// @param ring Destination history.
    /// @param stats Import counters.
    /// @param max_lines Maximum lines read in this call.
    /// @return `true` when the file is exhausted.
    bool step(RecordRing& ring, ImportStats& stats, std::size_t max_lines) noexcept;

    /// @brief Whether the file is exhausted.
    /// @return `true` after the last line.
    [[nodiscard]] bool done() const noexcept { return done_; }
    /// @brief Completion ratio in `0.0..1.0`.
    /// @return Bytes consumed divided by file size.
    [[nodiscard]] double ratio() const noexcept;
    /// @brief Cold-path diagnostic rendering.
    /// @return Text with path, progress, and completion.
    [[nodiscard]] std::string to_string() const;

private:
    explicit ImportSession(std::string path) : path_(std::move(path)) {}

    std::string path_{};        ///< Source path.
    std::ifstream file_{};      ///< Open stream.
    std::uint64_t total_{0};    ///< File size in bytes.
    std::uint64_t consumed_{0}; ///< Bytes consumed by complete lines.
    bool done_{false};          ///< End of file reached.
};

/// @brief Parse an ISO-8601 timestamp into a monotonic-free UTC nanosecond value.
/// @param text Timestamp text.
/// @param out Destination nanoseconds since the Unix epoch.
/// @return `true` when the text matched the fixed-width exported shape.
[[nodiscard]] bool parse_iso8601_ns(std::string_view text, std::int64_t& out) noexcept;

} // namespace nexigen::logview
