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

namespace {

/// @brief The seven numeric fields of a fixed-shape ISO-8601 UTC stamp.
struct IsoFields {
    std::int64_t year{0};
    std::int64_t month{0};
    std::int64_t day{0};
    std::int64_t hour{0};
    std::int64_t minute{0};
    std::int64_t second{0};
    std::int64_t nanos{0};
};

/// @brief Whether `text` has the exact `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ` shape.
///
/// @param text Candidate stamp.
/// @return `true` when every separator sits at its fixed offset.
[[nodiscard]] bool match_iso8601_shape(std::string_view text) noexcept {
    return text.size() == 30U && text[4] == '-' && text[7] == '-' && text[10] == 'T' &&
           text[13] == ':' && text[16] == ':' && text[19] == '.' && text[29] == 'Z';
}

/// @brief Read the seven numeric fields of a shaped ISO-8601 stamp.
///
/// @param text Stamp that already passed `match_iso8601_shape`.
/// @param fields Receives the parsed fields.
/// @return `true` when every field is all digits.
[[nodiscard]] bool extract_iso8601_fields(std::string_view text, IsoFields& fields) noexcept {
    return parse_digits(text, 0, 4, fields.year) && parse_digits(text, 5, 2, fields.month) &&
           parse_digits(text, 8, 2, fields.day) && parse_digits(text, 11, 2, fields.hour) &&
           parse_digits(text, 14, 2, fields.minute) && parse_digits(text, 17, 2, fields.second) &&
           parse_digits(text, 20, 9, fields.nanos);
}

/// @brief Convert civil UTC fields to a Unix-epoch nanosecond count.
///
/// Uses Howard Hinnant's `days_from_civil` algorithm.
///
/// @param fields Parsed stamp fields.
/// @return Nanoseconds since the Unix epoch.
[[nodiscard]] std::int64_t civil_to_epoch_ns(const IsoFields& fields) noexcept {
    const std::int64_t y = fields.year - (fields.month <= 2 ? 1 : 0);
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;
    const std::int64_t doy =
        (153 * (fields.month + (fields.month > 2 ? -3 : 9)) + 2) / 5 + fields.day - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = era * 146097 + doe - 719468;
    return (((days * 24 + fields.hour) * 60 + fields.minute) * 60 + fields.second) * 1'000'000'000 +
           fields.nanos;
}

/// @brief One record recovered from a JSON-lines or text line.
struct JsonRecord {
    std::string app{};
    std::string message{};
    std::uint64_t mono_ns{0};
    std::uint64_t thread_id{0};
    std::uint64_t pid{0};
    nexigen::core::LogLevel level{nexigen::core::LogLevel::Info};
    bool have_message{false};
};

} // namespace

bool parse_iso8601_ns(std::string_view text, std::int64_t& out) noexcept {
    // Fixed shape from chrono::format_utc_iso8601_ns: YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ
    IsoFields fields{};
    if (!match_iso8601_shape(text) || !extract_iso8601_fields(text, fields)) {
        return false;
    }
    out = civil_to_epoch_ns(fields);
    return true;
}

namespace {

/// @brief Fill one record from a parsed JSON object and update the UTC offset.
///
/// @param json Parsed object.
/// @param record Receives the fields.
/// @param stats Import statistics; first-seen UTC offset is recorded.
void fill_json_record(const nlohmann::json& json, JsonRecord& record, ImportStats& stats) noexcept {
    static_cast<void>(parse_int(json, "mono_ns", record.mono_ns));
    static_cast<void>(parse_int(json, "thread_id", record.thread_id));
    static_cast<void>(parse_int(json, "pid", record.pid));
    record.app = read_string(json, "app");
    record.message = read_string(json, "message");
    record.have_message = true;
    if (const std::string level_text = read_string(json, "level"); !level_text.empty()) {
        static_cast<void>(nexigen::core::from_string(level_text, record.level));
    }
    // The exported timestamp is UTC; mono_ns is authoritative for deltas.
    const std::string timestamp = read_string(json, "timestamp");
    std::int64_t parsed_utc = 0;
    const bool have_utc = parse_iso8601_ns(timestamp, parsed_utc);
    if (record.mono_ns == 0U && have_utc) {
        record.mono_ns = static_cast<std::uint64_t>(parsed_utc);
    }
    if (have_utc && record.mono_ns != 0U && !stats.utc_offset_ns.has_value()) {
        stats.utc_offset_ns = parsed_utc - static_cast<std::int64_t>(record.mono_ns);
    }
}

/// @brief Recover the fields of a JSON line that no longer parses.
///
/// The daemon writes the message with `%.*s`, so a quote or newline can break
/// the JSON. Only the message is mandatory; app and level are best-effort.
///
/// @param line Raw line.
/// @param record Receives the recovered fields.
/// @param stats Import statistics; a missing message counts as malformed.
/// @return `true` when the message field was recovered.
[[nodiscard]] bool recover_loose_record(std::string_view line, JsonRecord& record,
                                        ImportStats& stats) noexcept {
    const auto loose_message = loose_field(line, "message");
    if (!loose_message.has_value()) {
        ++stats.malformed;
        return false;
    }
    record.message = *loose_message;
    record.app = loose_field(line, "app").value_or("");
    if (const auto loose_level = loose_field(line, "level"); loose_level.has_value()) {
        static_cast<void>(nexigen::core::from_string(*loose_level, record.level));
    }
    record.have_message = true;
    return true;
}

/// @brief Track the first boot identifier and detect a mixed-boot file.
///
/// @param boot Boot identifier from the record, empty when absent.
/// @param stats Import statistics.
void note_boot(std::string_view boot, ImportStats& stats) noexcept {
    if (boot.empty()) {
        return;
    }
    if (stats.first_boot.empty()) {
        stats.first_boot = boot;
        return;
    }
    if (stats.first_boot != boot) {
        stats.mixed_boot = true;
    }
}

/// @brief Import one JSON-lines record.
///
/// @param line Raw line starting with `{`.
/// @param ring Destination ring.
/// @param stats Import statistics.
/// @return `true` when a record was appended.
[[nodiscard]] bool import_json_line(std::string_view line, RecordRing& ring,
                                    ImportStats& stats) noexcept {
    const nlohmann::json json = nlohmann::json::parse(line, nullptr, false);
    JsonRecord record{};
    if (!json.is_discarded() && json.is_object()) {
        fill_json_record(json, record, stats);
        note_boot(read_string(json, "boot"), stats);
    } else if (!recover_loose_record(line, record, stats)) {
        return false;
    }
    if (!record.have_message) {
        ++stats.malformed;
        return false;
    }
    ring.push_frame(make_frame(record.mono_ns, record.app, record.level,
                               static_cast<std::uint32_t>(record.thread_id),
                               static_cast<std::uint32_t>(record.pid), record.message));
    ++stats.imported;
    return true;
}

/// @brief Import one plain-text line with an inferred level.
///
/// @param line Raw line.
/// @param ring Destination ring.
/// @param stats Import statistics.
/// @return Always `true`.
[[nodiscard]] bool import_text_line(std::string_view line, RecordRing& ring,
                                    ImportStats& stats) noexcept {
    ++stats.plain;
    ring.push_frame(make_frame(0, "", infer_level(line), 0, 0, line));
    ++stats.imported;
    return true;
}

} // namespace

bool import_line(std::string_view line, RecordRing& ring, ImportStats& stats) noexcept {
    if (line.empty()) {
        return false;
    }
    if (line.front() == '{') {
        return import_json_line(line, ring, stats);
    }
    return import_text_line(line, ring, stats);
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
