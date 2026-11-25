// nexigen-canview — canonical/legacy session import.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "session_record.hpp"

namespace nexigen::canview {

/// @brief Result of parsing one session file.
struct ImportResult {
    CanViewSessionHeader header{};               ///< Session header or synthetic legacy header.
    std::vector<SessionRecord> records{};        ///< Imported frame/gap/status records.
    std::vector<SessionStatusRecord> statuses{}; ///< Imported status samples.
    std::size_t lines{0};                        ///< Lines read.
    std::size_t malformed{0};                    ///< Invalid lines skipped.
    std::size_t skipped{0};                      ///< Lines beyond the record limit.
    bool legacy{false};                          ///< Input had no session header.
    bool mixed_boot{false};                      ///< More than one boot token was seen.
    bool partial_tail{false};                    ///< The last line was incomplete.
    bool truncated{false};                       ///< Record limit reached.
    bool cancelled{false};                       ///< Import cancelled by the caller.
    bool unsupported_format{false};              ///< Input looked like CSV, not a session.
};

/// @brief Parses canonical JSONL v1 and legacy `nexigen-canmon --json`.
class SessionImporter {
public:
    /// @brief Look up the latest imported status for one channel.
    /// @param result Import result.
    /// @param channel_id Channel identity.
    /// @return Status record or `nullptr`.
    [[nodiscard]] static const SessionStatusRecord* status_for(const ImportResult& result,
                                                               std::uint16_t channel_id) noexcept;

    /// @brief Default maximum line size.
    static constexpr std::size_t default_max_line{1024U * 1024U};
    /// @brief Default maximum imported records.
    static constexpr std::size_t default_max_records{5'000'000U};

    /// @brief Parse in-memory text.
    /// @param content Source text.
    /// @param max_line Maximum accepted line size.
    /// @param max_records Maximum imported records.
    /// @return Import result with local sequence numbers.
    [[nodiscard]] static ImportResult parse(std::string_view content,
                                            std::size_t max_line = default_max_line,
                                            std::size_t max_records = default_max_records,
                                            const std::atomic<bool>* cancelled = nullptr);

    /// @brief Read and parse a file.
    /// @param path File path.
    /// @param max_line Maximum accepted line size.
    /// @param max_records Maximum imported records.
    /// @return Import result.
    [[nodiscard]] static ImportResult load(const std::string& path,
                                           std::size_t max_line = default_max_line,
                                           std::size_t max_records = default_max_records,
                                           const std::atomic<bool>* cancelled = nullptr);
};

} // namespace nexigen::canview
