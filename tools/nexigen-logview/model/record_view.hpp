// nexigen-logview — borrowed view of one retained record.
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nexigen/logging/log_frame.hpp>

#include "record_kind.hpp"

namespace nexigen::logview {

/// @brief Borrowed view of one history slot.
///
/// The view points into the ring and is valid until the next ingest. Render and
/// inspector code reads it immediately and never stores it.
struct RecordView {
    std::uint64_t seq{0};                      ///< Absolute record number.
    RecordKind kind{RecordKind::Frame};        ///< Frame or synthetic loss.
    const nexigen::logging::LogFrame* frame{}; ///< Borrowed frame payload.
    std::int64_t prev_same_app_ns{0};          ///< Per-application delta at ingest.
    std::uint64_t gap_count{0};                ///< Missing count for synthetic records.

    /// @brief Whether the view is a received frame.
    /// @return `true` for `RecordKind::Frame`.
    [[nodiscard]] bool is_frame() const noexcept { return kind == RecordKind::Frame; }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text naming the sequence, kind, and gap count.
    [[nodiscard]] std::string to_string() const {
        return std::format("RecordView(seq={}, kind={}, gap={})", seq, kind, gap_count);
    }
};

} // namespace nexigen::logview
