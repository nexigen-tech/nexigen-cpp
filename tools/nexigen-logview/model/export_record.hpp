// nexigen-logview — one record copied into the export writer queue.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <nexigen/logging/log_frame.hpp>

#include "record_kind.hpp"

namespace nexigen::logview {

/// @brief Self-contained copy of one record for asynchronous export.
struct alignas(64) ExportRecord {
    nexigen::logging::LogFrame frame{};  ///< Copied frame.
    std::uint64_t seq{0};                ///< Absolute record number.
    std::int64_t previous_visible_ns{0}; ///< Delta against the previous visible record.
    std::int64_t previous_app_ns{0};     ///< Delta against the previous same-app record.
    RecordKind kind{RecordKind::Frame};  ///< Frame or synthetic loss.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text naming the record.
    [[nodiscard]] std::string to_string() const;
};

static_assert(sizeof(ExportRecord) == 320, "ExportRecord must round to five cache lines");
static_assert(alignof(ExportRecord) == 64, "ExportRecord must be cache-line aligned");

/// @brief Free diagnostic mirror of `ExportRecord::to_string`.
/// @param record Record to render.
/// @return The same text.
[[nodiscard]] inline std::string to_string(const ExportRecord& record) {
    return record.to_string();
}

} // namespace nexigen::logview
