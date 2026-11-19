// nexigen::chrono — time-base history file writer (change
// add-time-base-service; spec chrono/time-base "Coefficient history").
//
// Writes one JSON object per appended history entry (a low-rate stream: one
// line per published change, never per interval). The file is opened once at
// daemon start and fsync'd on shutdown or on an explicit flush. This is cold
// path: building the JSON line allocates, and that is acceptable here because
// the stream carries at most a handful of lines per second.
#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/json_file_sink_config.hpp>
#include <nexigen/chrono/time_base_history_entry.hpp>
#include <nexigen/chrono/time_base_publisher.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// @brief Append-only JSON-lines writer for the time-base history.
///
/// Implements the publisher's history sink through a static thunk, so the
/// daemon can hand the publisher a `HistorySink` bound to this object.
///
/// @note Spec `chrono/time-base`, requirement "Coefficient history".
class TimeBaseHistoryWriter {
public:
    /// @brief Open the configured history file.
    ///
    /// An unopenable path leaves the writer closed; `append()` then reports
    /// failure but never aborts.
    ///
    /// @param config Path and append policy copied at construction.
    explicit TimeBaseHistoryWriter(const JsonFileSinkConfig& config) noexcept;

    /// @brief Copying is deleted: the writer owns a descriptor.
    TimeBaseHistoryWriter(const TimeBaseHistoryWriter&) = delete;
    /// @brief Copy assignment is deleted: the writer owns a descriptor.
    TimeBaseHistoryWriter& operator=(const TimeBaseHistoryWriter&) = delete;

    /// @brief Close the owned descriptor.
    ~TimeBaseHistoryWriter();

    /// @brief Append one history entry as a single JSON line.
    ///
    /// @param entry Entry to append.
    /// @return `true` when the whole line was written.
    /// @note Cold path only.
    [[nodiscard]] bool append(const TimeBaseHistoryEntry& entry) noexcept;

    /// @brief Push written data to the file with `fsync`.
    ///
    /// @return Success, or `SystemError` when the file is not open or the flush fails.
    [[nodiscard]] core::Status flush() noexcept;

    /// @brief Whether the file was opened successfully.
    ///
    /// @return `true` when the writer owns a descriptor.
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

    /// @brief The publisher-facing sink bound to this writer.
    ///
    /// @return Function-table sink; valid for the writer's lifetime.
    [[nodiscard]] TimeBasePublisher::HistorySink sink() noexcept {
        return TimeBasePublisher::HistorySink{this, &TimeBaseHistoryWriter::append_thunk};
    }

    /// @brief Render the writer's diagnostic identity.
    ///
    /// @return Text of the form `TimeBaseHistoryWriter(fd=<descriptor>, open=<bool>)`.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Static bridge from the publisher's sink table to `append`.
    ///
    /// @param self `TimeBaseHistoryWriter` pointer.
    /// @param entry Entry to append.
    /// @return `append()` result.
    static bool append_thunk(void* self, const TimeBaseHistoryEntry& entry) noexcept;

    int fd_{-1}; ///< Owned output descriptor; `-1` when closed.
};

/// @brief Free-helper mirror of `TimeBaseHistoryWriter::to_string`.
///
/// @param writer Writer to render.
/// @return The same text as the member.
/// @note Project mandate 9.
[[nodiscard]] std::string to_string(const TimeBaseHistoryWriter& writer);

/// @brief Serialize the writer as diagnostic JSON (cold path).
///
/// @param j Destination JSON value.
/// @param writer Writer being described.
/// @note Project mandate 10.
inline void to_json(nlohmann::json& j, const TimeBaseHistoryWriter& writer) {
    j = nlohmann::json{{"type", "TimeBaseHistoryWriter"}, {"open", writer.is_open()}};
}

} // namespace nexigen::chrono
