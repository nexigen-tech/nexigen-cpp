#include "importer.hpp"

#include <array>
#include <cstdio>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <format>
#include <optional>

#include <nlohmann/json.hpp>

#include <nexigen/core/log_level.hpp>

namespace nexigen::logview {

namespace {

bool parse_int(const nlohmann::json& j, const char* key, std::uint64_t& out) noexcept {
    if (const auto it = j.find(key); it != j.end() && it->is_number_unsigned()) {
        out = it->get<std::uint64_t>();
        return true;
    }
    return false;
}

std::string read_string(const nlohmann::json& j, const char* key) {
    if (const auto it = j.find(key); it != j.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return {};
}

nexigen::core::LogLevel infer_level(std::string_view line) noexcept {
    if (line.find("FATAL") != std::string_view::npos ||
        line.find("fatal") != std::string_view::npos) {
        return nexigen::core::LogLevel::Fatal;
    }
    if (line.find("ERROR") != std::string_view::npos ||
        line.find("error") != std::string_view::npos) {
        return nexigen::core::LogLevel::Error;
    }
    if (line.find("WARN") != std::string_view::npos ||
        line.find("warn") != std::string_view::npos) {
        return nexigen::core::LogLevel::Warning;
    }
    if (line.find("DEBUG") != std::string_view::npos ||
        line.find("debug") != std::string_view::npos) {
        return nexigen::core::LogLevel::Debug;
    }
    return nexigen::core::LogLevel::Info;
}

nexigen::logging::LogFrame make_frame(std::uint64_t stamp_ns, std::string_view app,
                                      nexigen::core::LogLevel level, std::uint32_t thread_id,
                                      std::uint32_t pid, std::string_view message) noexcept {
    nexigen::logging::LogFrame frame{};
    frame.timestamp_ns = stamp_ns;
    frame.log_level = level;
    frame.thread_id = thread_id;
    frame.pid = pid;
    const std::size_t app_size =
        app.size() < sizeof(frame.app_id) ? app.size() : sizeof(frame.app_id);
    if (app_size > 0U) {
        std::memcpy(frame.app_id, app.data(), app_size);
    }
    const std::size_t message_size =
        message.size() < sizeof(frame.payload) ? message.size() : sizeof(frame.payload);
    if (message_size > 0U) {
        std::memcpy(frame.payload, message.data(), message_size);
    }
    frame.payload_size = static_cast<std::uint16_t>(message_size);
    return frame;
}

/// Extract one `"key":"..."` field from a line whose JSON is otherwise broken by
/// an unescaped message. This is the daemon compatibility path.
std::optional<std::string> loose_field(std::string_view line, std::string_view key) noexcept {
    std::array<char, 32> needle{};
    const int n = std::snprintf(needle.data(), needle.size(), "\"%.*s\":\"",
                                static_cast<int>(key.size()), key.data());
    if (n <= 0) {
        return std::nullopt;
    }
    const std::string_view marker(needle.data(), static_cast<std::size_t>(n));
    const std::size_t start = line.find(marker);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t begin = start + marker.size();
    std::string out;
    for (std::size_t i = begin; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1U < line.size()) {
            out.push_back(line[i + 1U]);
            ++i;
        } else if (line[i] == '"') {
            return out;
        } else {
            out.push_back(line[i]);
        }
    }
    return std::nullopt;
}

} // namespace

std::string ImportStats::to_string() const {
    return std::format("ImportStats(imported={}, malformed={}, plain={}, mixed_boot={}, boot={})",
                       imported, malformed, plain, mixed_boot, first_boot);
}

namespace {

bool parse_digits(std::string_view text, std::size_t offset, std::size_t width,
                  std::int64_t& value) noexcept {
    value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        const char ch = text[offset + i];
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = value * 10 + static_cast<std::int64_t>(ch - '0');
    }
    return true;
}

} // namespace

bool parse_iso8601_ns(std::string_view text, std::int64_t& out) noexcept {
    // Fixed shape from chrono::format_utc_iso8601_ns: YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ
    if (text.size() != 30U || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':' || text[19] != '.' || text[29] != 'Z') {
        return false;
    }
    std::int64_t year = 0;
    std::int64_t month = 0;
    std::int64_t day = 0;
    std::int64_t hour = 0;
    std::int64_t minute = 0;
    std::int64_t second = 0;
    std::int64_t nanos = 0;
    if (!parse_digits(text, 0, 4, year) || !parse_digits(text, 5, 2, month) ||
        !parse_digits(text, 8, 2, day) || !parse_digits(text, 11, 2, hour) ||
        !parse_digits(text, 14, 2, minute) || !parse_digits(text, 17, 2, second) ||
        !parse_digits(text, 20, 9, nanos)) {
        return false;
    }
    // Howard Hinnant's days_from_civil.
    const std::int64_t y = year - (month <= 2 ? 1 : 0);
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;
    const std::int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = era * 146097 + doe - 719468;
    out = (((days * 24 + hour) * 60 + minute) * 60 + second) * 1'000'000'000 + nanos;
    return true;
}

bool import_line(std::string_view line, RecordRing& ring, ImportStats& stats) noexcept {
    if (line.empty()) {
        return false;
    }
    if (line.front() == '{') {
        nlohmann::json json = nlohmann::json::parse(line, nullptr, false);
        std::string app;
        std::string message;
        std::uint64_t mono_ns = 0;
        std::uint64_t thread_id = 0;
        std::uint64_t pid = 0;
        nexigen::core::LogLevel level = nexigen::core::LogLevel::Info;
        bool have_message = false;
        if (!json.is_discarded() && json.is_object()) {
            static_cast<void>(parse_int(json, "mono_ns", mono_ns));
            static_cast<void>(parse_int(json, "thread_id", thread_id));
            static_cast<void>(parse_int(json, "pid", pid));
            app = read_string(json, "app");
            message = read_string(json, "message");
            have_message = true;
            if (const std::string level_text = read_string(json, "level"); !level_text.empty()) {
                static_cast<void>(nexigen::core::from_string(level_text, level));
            }
            if (const auto it = json.find("timestamp"); it != json.end() && it->is_string()) {
                // The exported timestamp is UTC; mono_ns is authoritative for deltas.
                static_cast<void>(it);
            }
            const std::string timestamp = read_string(json, "timestamp");
            std::int64_t parsed_utc = 0;
            const bool have_utc = parse_iso8601_ns(timestamp, parsed_utc);
            if (mono_ns == 0U && have_utc) {
                mono_ns = static_cast<std::uint64_t>(parsed_utc);
            }
            if (have_utc && mono_ns != 0U && !stats.utc_offset_ns.has_value()) {
                stats.utc_offset_ns = parsed_utc - static_cast<std::int64_t>(mono_ns);
            }
        } else {
            // The daemon writes the message with %.*s, so a quote or newline can
            // break the JSON. Recover the fields when possible.
            const auto loose_message = loose_field(line, "message");
            if (!loose_message.has_value()) {
                ++stats.malformed;
                return false;
            }
            message = *loose_message;
            app = loose_field(line, "app").value_or("");
            if (const auto loose_level = loose_field(line, "level"); loose_level.has_value()) {
                static_cast<void>(nexigen::core::from_string(*loose_level, level));
            }
            have_message = true;
        }
        if (!have_message) {
            ++stats.malformed;
            return false;
        }
        const std::string boot = json.is_discarded() ? std::string{} : read_string(json, "boot");
        if (!boot.empty()) {
            if (stats.first_boot.empty()) {
                stats.first_boot = boot;
            } else if (stats.first_boot != boot) {
                stats.mixed_boot = true;
            }
        }
        ring.push_frame(make_frame(mono_ns, app, level, static_cast<std::uint32_t>(thread_id),
                                   static_cast<std::uint32_t>(pid), message));
        ++stats.imported;
        return true;
    }
    ++stats.plain;
    ring.push_frame(make_frame(0, "", infer_level(line), 0, 0, line));
    ++stats.imported;
    return true;
}

nexigen::core::Result<std::unique_ptr<ImportSession>> ImportSession::open(const std::string& path) {
    auto session = std::unique_ptr<ImportSession>(new ImportSession(path));
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0 || info.st_size < 0) {
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    session->total_ = static_cast<std::uint64_t>(info.st_size);
    session->file_.open(path);
    if (!session->file_) {
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    return session;
}

std::string ImportSession::to_string() const {
    return std::format("ImportSession(path={}, consumed={}, total={}, done={})", path_, consumed_,
                       total_, done_);
}

double ImportSession::ratio() const noexcept {
    if (total_ == 0U) {
        return done_ ? 1.0 : 0.0;
    }
    return static_cast<double>(consumed_) / static_cast<double>(total_);
}

bool ImportSession::step(RecordRing& ring, ImportStats& stats, std::size_t max_lines) noexcept {
    if (done_) {
        return true;
    }
    const std::size_t chunk = max_lines == 0U ? 1U : max_lines;
    std::string line;
    for (std::size_t i = 0; i < chunk; ++i) {
        if (!std::getline(file_, line)) {
            done_ = true;
            break;
        }
        consumed_ += line.size() + 1U;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        static_cast<void>(import_line(line, ring, stats));
    }
    if (file_.eof()) {
        done_ = true;
    }
    if (consumed_ > total_) {
        consumed_ = total_;
    }
    return done_;
}

nexigen::core::Status import_file(const std::string& path, RecordRing& ring,
                                  ImportStats& stats) noexcept {
    std::ifstream file(path);
    if (!file) {
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        static_cast<void>(import_line(line, ring, stats));
    }
    return {};
}

} // namespace nexigen::logview
