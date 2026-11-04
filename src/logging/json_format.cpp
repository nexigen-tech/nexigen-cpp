// nexigen::logging — JSON line implementation (design.md D12). See
// detail/json_writer.hpp for the schema and escaping contract.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/chrono/time_format.hpp>
#include <nexigen/logging/detail/json_writer.hpp>
#include <nexigen/logging/log_level.hpp>

namespace nexigen::logging::detail {

using namespace std::literals::string_view_literals; // for the schema string literals

std::size_t format_json_line(std::int64_t utc_ns, LogLevel level, std::string_view app,
                             std::uint32_t thread_id, std::string_view file_name,
                             std::uint32_t line, std::string_view function_name,
                             std::string_view message,
                             char (&scratch)[json_scratch_capacity]) noexcept {
    char ts[31];
    chrono::format_utc_iso8601_ns(utc_ns, ts);

    FixedWriter w{scratch, json_scratch_capacity};
    static_cast<void>(w.append_sv("{\"timestamp\":\""sv));
    static_cast<void>(w.append_sv(std::string_view(ts, 30)));
    static_cast<void>(w.append_sv("\",\"level\":\""sv));
    static_cast<void>(w.append_sv(to_string(level)));
    static_cast<void>(w.append_sv("\",\"app\":\""sv));
    static_cast<void>(w.append_escaped(app));
    static_cast<void>(w.append_sv("\",\"thread_id\":"sv));
    static_cast<void>(w.append_u64(thread_id));
    static_cast<void>(w.append_sv(",\"message\":\"["sv));
    // Message body: "[<basename>:<line>] (<function>) <user message>" with
    // each piece JSON-escaped in place (escaping composes over concatenation).
    const std::size_t slash = file_name.find_last_of('/');
    const std::string_view base =
        (slash == std::string_view::npos) ? file_name : file_name.substr(slash + 1);
    static_cast<void>(w.append_escaped(base));
    static_cast<void>(w.append_char(':'));
    static_cast<void>(w.append_u64(line));
    static_cast<void>(w.append_sv("] ("sv));
    static_cast<void>(w.append_escaped(function_name));
    static_cast<void>(w.append_sv(") "sv));
    static_cast<void>(w.append_escaped(message));
    static_cast<void>(w.append_sv("\"}\n"sv));

    if (!w.ok()) {
        return 0; // overflowing line is skipped by the caller, never half-emitted
    }
    return w.size();
}

} // namespace nexigen::logging::detail
