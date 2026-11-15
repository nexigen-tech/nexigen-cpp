// nexigen::hub — SSE formatting helpers
// (quality-tail-cleanup; P3-30).
#include "http_sse.hpp"

#include <format>

namespace nexigen::hub::detail {

std::string json_text(const nlohmann::json& document) {
    return document.dump();
}

bool starts_with(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

std::string sse_record(const StreamRecord& record) {
    nlohmann::json document;
    to_json(document, record);
    return "event: record\nid: " + record.stream_id + ":" + std::to_string(record.sequence) +
           "\ndata: " + json_text(document) + "\n\n";
}

std::string sse_gap(const GapMarker& gap) {
    nlohmann::json document;
    to_json(document, gap);
    return "event: gap\ndata: " + json_text(document) + "\n\n";
}

std::string sse_headers() noexcept {
    return "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
           "Cache-Control: no-cache\r\nTransfer-Encoding: chunked\r\n"
           "X-Accel-Buffering: no\r\n\r\n";
}

std::string sse_tree_event(const nlohmann::json& document) noexcept {
    return "event: tree\ndata: " + json_text(document) + "\n\n";
}

} // namespace nexigen::hub::detail
