// nexigen-logview — inspector text helpers shared by the UI and tests.
#pragma once

#include <string>

#include <nexigen/logging/log_frame.hpp>

#include "record_view.hpp"
#include "time_formatter.hpp"

namespace nexigen::logview {

/// @brief Render one record as the inspector clipboard payload.
/// @param view Record to render.
/// @param stamp Already formatted timestamp for the record.
/// @return Metadata plus the complete payload.
[[nodiscard]] std::string inspector_clipboard(const RecordView& view, const FormattedTime& stamp);

/// @brief Whether a payload filled the producer frame capacity.
/// @param frame Frame to test.
/// @return `true` when the producer may have truncated the message.
[[nodiscard]] bool payload_may_be_truncated(const nexigen::logging::LogFrame& frame) noexcept;

} // namespace nexigen::logview
