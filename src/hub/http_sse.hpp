// nexigen::hub — SSE formatting helpers extracted from HttpServer
// (quality-tail-cleanup; P3-30).
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#include <nexigen/hub/gap_marker.hpp>
#include <nexigen/hub/stream_record.hpp>

namespace nexigen::hub::detail {

[[nodiscard]] std::string json_text(const nlohmann::json& document);
[[nodiscard]] bool starts_with(std::string_view text, std::string_view prefix) noexcept;
[[nodiscard]] std::string sse_record(const StreamRecord& record);
[[nodiscard]] std::string sse_gap(const GapMarker& gap);
[[nodiscard]] std::string sse_headers() noexcept;
[[nodiscard]] std::string sse_tree_event(const nlohmann::json& document) noexcept;

} // namespace nexigen::hub::detail
