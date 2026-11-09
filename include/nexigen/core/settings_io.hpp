// nexigen::core — settings-io helpers shared by the logging / metrics /
// tracing settings loaders (change fix-p2-observability-and-writers,
// analysis #25: three near-identical copy groups collapse into one;
// dedup-sink-plumbing extends the vocabulary with the per-sink key reads
// that all three loaders also carried as copies).
//
// DETAIL DISCLAIMER: the namespace ship keeps a dedicated name
// (`nexigen::core::settings_io`) so `using namespace nexigen::core` test
// scopes never learn a SECOND bare `detail` candidate name (see the
// FixedWriter note). The helpers are small, exception-free and cold-path.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <limits>
#include <string>
#include <string_view>
#include <charconv>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// nlohmann/json triggers a -Wnull-dereference warning under -O3
// (pre-existing upstream issue); the pragma guards this cold unit only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <nexigen/core/result.hpp>
#include <nexigen/core/settings_diagnostic.hpp>
#include <nexigen/core/settings_paths.hpp>
#include <nexigen/core/settings_source.hpp>
#include <nexigen/core/thread_management.hpp>

namespace nexigen::core::settings_io {

/// File-read outcome for optional settings sources (same tri-state the
/// per-module loaders carried).
enum class FileRead : std::uint8_t { Absent, Ok, Failed };

[[nodiscard]] inline FileRead read_text_file(std::string_view path, std::string& out) noexcept {
    if (path.empty()) {
        return FileRead::Absent;
    }
    const std::string path_buf{path};
    const int fd = ::open(path_buf.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return errno == ENOENT ? FileRead::Absent : FileRead::Failed;
    }
    struct stat st{};
    if (::fstat(fd, &st) != 0 || st.st_size < 0) {
        static_cast<void>(::close(fd));
        return FileRead::Failed;
    }
    out.resize(static_cast<std::size_t>(st.st_size));
    std::size_t offset = 0;
    while (offset < out.size()) {
        const ssize_t got = ::read(fd, out.data() + offset, out.size() - offset);
        if (got > 0) {
            offset += static_cast<std::size_t>(got);
        } else if (got == 0) {
            break;
        } else if (errno != EINTR) {
            static_cast<void>(::close(fd));
            return FileRead::Failed;
        }
    }
    out.resize(offset);
    static_cast<void>(::close(fd));
    return FileRead::Ok;
}

/// Numeric contract part 1: an unsigned JSON number.
[[nodiscard]] inline bool unsigned_value(const nlohmann::json& value, std::uint64_t& out) noexcept {
    if (!value.is_number_unsigned()) {
        return false;
    }
    out = value.get<std::uint64_t>();
    return true;
}

/// Numeric contract part 2: a full-token decimal digit string.
[[nodiscard]] inline bool parse_u64(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

/// Append one merge-layer finding to a settings diagnostics vector (the
/// type every loader carries; the logging loader forwards its Settings'
/// vector here).
inline void add_diagnostic(std::vector<SettingsDiagnostic>& diagnostics, SettingsSource source,
                           std::string field, std::string message) {
    diagnostics.push_back(SettingsDiagnostic{source, std::move(field), std::move(message)});
}

// --- loader vocabulary (dedup-sink-plumbing): the per-key read helpers the
// three module loaders carried as verbatim copies. Diagnostic texts are
// byte-identical to the per-module originals; an absent key leaves the
// target untouched and produces no diagnostic, an invalid value reports
// once and leaves the previous target intact.

/// Read a JSON key into a uint64 target when present and within its bound.
inline void read_uint(const nlohmann::json& document, const char* key, std::uint64_t max_value,
                      std::uint64_t& target, std::vector<SettingsDiagnostic>& diagnostics,
                      SettingsSource source) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return;
    }
    std::uint64_t value = 0;
    if (unsigned_value(*it, value) && value <= max_value) {
        target = value;
    } else {
        add_diagnostic(diagnostics, source, key, "expected an integer in range");
    }
}

/// The "sink.<key>" nested-field name shared by every sink-reader diagnostic.
[[nodiscard]] inline std::string sink_field(const char* key) {
    return std::string("sink.") + key;
}

/// Read an optional string key out of the sink object.
inline void read_sink_string(const nlohmann::json& sink, const char* key, std::string& target,
                             std::vector<SettingsDiagnostic>& diagnostics, SettingsSource source) {
    const auto it = sink.find(key);
    if (it == sink.end()) {
        return;
    }
    if (!it->is_string()) {
        add_diagnostic(diagnostics, source, sink_field(key), "expected a string");
        return;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text != nullptr) {
        target = std::string(*text);
    }
}

/// Read an optional boolean key out of the sink object.
inline void read_sink_bool(const nlohmann::json& sink, const char* key, bool& target,
                           std::vector<SettingsDiagnostic>& diagnostics, SettingsSource source) {
    const auto it = sink.find(key);
    if (it == sink.end()) {
        return;
    }
    if (!it->is_boolean()) {
        add_diagnostic(diagnostics, source, sink_field(key), "expected a boolean");
        return;
    }
    target = it->get<bool>();
}

/// Read an optional unsigned key out of the sink object.
inline void read_sink_uint(const nlohmann::json& sink, const char* key, std::uint64_t& target,
                           std::vector<SettingsDiagnostic>& diagnostics, SettingsSource source) {
    const auto it = sink.find(key);
    if (it == sink.end()) {
        return;
    }
    std::uint64_t value = 0;
    if (unsigned_value(*it, value)) {
        target = value;
    } else {
        add_diagnostic(diagnostics, source, sink_field(key), "expected a non-negative integer");
    }
}

/// Locate the optional sink object. Returns nullptr when absent or invalid;
/// an invalid value is reported once and leaves the previous sink intact.
[[nodiscard]] inline const nlohmann::json* find_sink(const nlohmann::json& document,
                                                     std::vector<SettingsDiagnostic>& diagnostics,
                                                     SettingsSource source) {
    const auto it = document.find("sink");
    if (it == document.end()) {
        return nullptr;
    }
    if (!it->is_object()) {
        add_diagnostic(diagnostics, source, "sink", "expected a JSON object");
        return nullptr;
    }
    return &*it;
}

// --- declarative field tables (refactor-settings-table-parsing, design D1-D4):
// one driver per direction; per-module tables list {key, hook}. Table order =
// read/write order, so diagnostics order and JSON key order are preserved by
// construction. Hooks are plain function pointers taking their own key — the
// field MEANINGS below are shared one-time implementations, module hooks are
// one-liners binding key + target member (no per-module copy of the parsing
// logic, which is what the clone scan flagged).

/// One document field: the key names it in JSON, the hook reads it.
template <class Settings> struct ReadField {
    const char* key;
    void (*read)(const char* key, const nlohmann::json& document, SettingsSource source,
                 std::vector<SettingsDiagnostic>& diagnostics, Settings& settings);
};

/// One document field of the sink sub-object: same contract, `sink` is the
/// object handed over by find_sink.
template <class Settings> struct SinkReadField {
    const char* key;
    void (*read)(const char* key, const nlohmann::json& sink, SettingsSource source,
                 std::vector<SettingsDiagnostic>& diagnostics, Settings& settings);
};

/// One JSON write slot: the key names it, the hook fills it.
template <class Settings> struct WriteField {
    const char* key;
    void (*write)(nlohmann::json& object, const char* key, const Settings& settings);
};

/// Nanoseconds field with the int64 ceiling: staged through uint64, committed
/// only when no diagnostic was appended (the collect_interval pattern).
inline void read_duration_ns(const nlohmann::json& document, const char* key, SettingsSource source,
                             std::vector<SettingsDiagnostic>& diagnostics,
                             std::chrono::nanoseconds& target) {
    std::uint64_t value = static_cast<std::uint64_t>(target.count() < 0 ? 0 : target.count());
    const std::size_t before = diagnostics.size();
    read_uint(document, key, static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
              value, diagnostics, source);
    if (diagnostics.size() == before) {
        target = std::chrono::nanoseconds{static_cast<std::int64_t>(value)};
    }
}

/// Legacy core_mask into a ThreadSpec: parsed with the uint vocabulary, the
/// spec applies it whenever the key is PRESENT (even if invalid — the mask
/// stays 0 then, matching the historical merge order).
inline void read_core_mask_into(const nlohmann::json& document, const char* key,
                                SettingsSource source, std::vector<SettingsDiagnostic>& diagnostics,
                                ThreadSpec& spec) {
    std::uint64_t core_mask = 0;
    read_uint(document, key, std::numeric_limits<std::uint64_t>::max(), core_mask, diagnostics,
              source);
    if (document.contains(key)) {
        spec.set_cpus(CpuSet::from_core_mask(core_mask));
    }
}

/// Full worker_thread object parsed through the shared ThreadSpec vocabulary.
inline void read_worker_spec(const nlohmann::json& document, const char* key, SettingsSource source,
                             std::vector<SettingsDiagnostic>& diagnostics, ThreadSpec& spec) {
    const auto worker = document.find(key);
    if (worker == document.end()) {
        return;
    }
    ThreadSpec parsed = spec;
    if (parse_thread_spec_json(*worker, parsed)) {
        spec = parsed;
    } else {
        add_diagnostic(diagnostics, source, key,
                       "expected valid name/cpus/policy/priority/mode fields");
    }
}

/// uint32 field staged through uint64 with a bound; committed only when no
/// diagnostic was appended (the max_* counters pattern).
inline void read_bounded_u32(const nlohmann::json& document, const char* key, SettingsSource source,
                             std::vector<SettingsDiagnostic>& diagnostics, std::uint32_t& target) {
    std::uint64_t value = target;
    const std::size_t before = diagnostics.size();
    read_uint(document, key, std::numeric_limits<std::uint32_t>::max(), value, diagnostics, source);
    if (diagnostics.size() == before) {
        target = static_cast<std::uint32_t>(value);
    }
}

/// uint32 field staged through uint64, assigned unconditionally (invalid
/// reads leave the staged copy of the old value — net no-op) — the
/// worker_poll_interval_us pattern.
inline void read_staged_u32(const nlohmann::json& document, const char* key,
                            std::uint64_t max_value, SettingsSource source,
                            std::vector<SettingsDiagnostic>& diagnostics, std::uint32_t& target) {
    std::uint64_t value = target;
    read_uint(document, key, max_value, value, diagnostics, source);
    target = static_cast<std::uint32_t>(value);
}

/// uint64 field read through the uint vocabulary and assigned unconditionally
/// (invalid reads keep the staged copy of the old value — net no-op), the
/// max_spans/window_ns pattern.
inline void read_staged_u64(const nlohmann::json& document, const char* key,
                            std::uint64_t max_value, SettingsSource source,
                            std::vector<SettingsDiagnostic>& diagnostics, std::uint64_t& target) {
    std::uint64_t value = target;
    read_uint(document, key, max_value, value, diagnostics, source);
    target = value;
}

/// Enum vocabulary field inside the sink object: string type-checked, parsed
/// through the enum's own ADL from_string; `allowed` is the historical
/// wording of the rejection diagnostic.
template <class Enum>
inline void read_sink_enum(const nlohmann::json& sink, const char* key, SettingsSource source,
                           std::vector<SettingsDiagnostic>& diagnostics, Enum& target,
                           const char* allowed) {
    const auto it = sink.find(key);
    if (it == sink.end()) {
        return;
    }
    if (!it->is_string()) {
        add_diagnostic(diagnostics, source, sink_field(key), "expected a string");
        return;
    }
    const auto* const name = it->get_ptr<const nlohmann::json::string_t*>();
    Enum parsed = target;
    if (name != nullptr && from_string(std::string_view(*name), parsed)) {
        target = parsed;
    } else {
        add_diagnostic(diagnostics, source, sink_field(key), allowed);
    }
}

/// Run the document table in order.
template <class Settings, std::size_t N>
inline void read_fields(const nlohmann::json& document, SettingsSource source,
                        const ReadField<Settings> (&fields)[N], Settings& settings) {
    for (const ReadField<Settings>& field : fields) {
        field.read(field.key, document, source, settings.diagnostics, settings);
    }
}

/// Run the sink table: find_sink once, then every field reads from it. A
/// missing or invalid sink produces the one shared diagnostic and stops.
template <class Settings, std::size_t N>
inline void read_sink_fields(const nlohmann::json& document, SettingsSource source,
                             const SinkReadField<Settings> (&fields)[N], Settings& settings) {
    const nlohmann::json* const sink = find_sink(document, settings.diagnostics, source);
    if (sink == nullptr) {
        return;
    }
    for (const SinkReadField<Settings>& field : fields) {
        field.read(field.key, *sink, source, settings.diagnostics, settings);
    }
}

/// Run the write table in order.
template <class Settings, std::size_t N>
inline void write_fields(nlohmann::json& object, const WriteField<Settings> (&fields)[N],
                         const Settings& settings) {
    for (const WriteField<Settings>& field : fields) {
        field.write(object, field.key, settings);
    }
}

/// The diagnostics array every module settings JSON ends with (shared shape;
/// the writer body lives in the SettingsDiagnostic ADL pair).
inline void write_diagnostics(nlohmann::json& object,
                              const std::vector<SettingsDiagnostic>& diagnostics) {
    nlohmann::json entries = nlohmann::json::array();
    for (const SettingsDiagnostic& diagnostic : diagnostics) {
        nlohmann::json entry;
        to_json(entry, diagnostic);
        entries.push_back(std::move(entry));
    }
    object["diagnostics"] = std::move(entries);
}

/// The identical `Settings(global_loaded=.., local_loaded=.., options=..,
/// sink=.., diagnostics=[..])` summary line every module settings struct
/// renders (design D6): the modules pass their own to_string views, the
/// joining/living lives here once.
[[nodiscard]] inline std::string
format_settings_summary(bool global_loaded, bool local_loaded, std::string_view options,
                        std::string_view sink, const std::vector<SettingsDiagnostic>& diagnostics) {
    std::string out = "Settings(global_loaded=" + std::string(global_loaded ? "true" : "false") +
                      ", local_loaded=" + std::string(local_loaded ? "true" : "false") +
                      ", options=" + std::string(options) + ", sink=" + std::string(sink) +
                      ", diagnostics=[";
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
        if (i != 0) {
            out += "; ";
        }
        out += diagnostics[i].to_string();
    }
    out += "])";
    return out;
}

/// The three-state settings loader skeleton (design D5): reset, global file,
/// local file, environment overlay, error precedence SystemError >
/// InvalidArgument. `Settings` must carry global_loaded/local_loaded/
/// diagnostics; `merge` and `apply_env` are the module's own field-table merge
/// and env appliers. Byte-identical diagnostics to the per-module bodies this
/// replaces (same wordings, same order).
template <class Settings>
[[nodiscard]] inline core::Status load_settings_document(
    const SettingsPaths& paths, Settings& out,
    void (*merge)(const nlohmann::json& document, SettingsSource source, Settings& settings),
    void (*apply_env)(Settings& settings)) {
    out = Settings{};
    bool invalid = false;
    bool system_error = false;
    std::string text;

    const FileRead global_read = read_text_file(paths.global_file, text);
    if (global_read == FileRead::Ok) {
        out.global_loaded = true;
        const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out.diagnostics, SettingsSource::Global, "", "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            merge(document, SettingsSource::Global, out);
            invalid = invalid || out.diagnostics.size() != before;
        }
    } else if (global_read == FileRead::Failed) {
        system_error = true;
        add_diagnostic(out.diagnostics, SettingsSource::Global, "",
                       "file exists but could not be read");
    }

    text.clear();
    const FileRead local_read = read_text_file(paths.local_file, text);
    if (local_read == FileRead::Ok) {
        out.local_loaded = true;
        const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out.diagnostics, SettingsSource::Local, "", "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            merge(document, SettingsSource::Local, out);
            invalid = invalid || out.diagnostics.size() != before;
        }
    } else if (local_read == FileRead::Failed) {
        system_error = true;
        add_diagnostic(out.diagnostics, SettingsSource::Local, "",
                       "file exists but could not be read");
    }

    const std::size_t before_env = out.diagnostics.size();
    apply_env(out);
    invalid = invalid || out.diagnostics.size() != before_env;

    if (system_error) {
        return std::unexpected(ErrorCode::SystemError);
    }
    if (invalid) {
        return std::unexpected(ErrorCode::InvalidArgument);
    }
    return {};
}

// --- environment overlay vocabulary (design D7): the per-module
// apply_*_environment scaffolding, once. env_value reads the variable;
// apply_env_fields walks the module table; the typed helpers carry the exact
// wordings the hand-written blocks produced.

/// Raw environment value or an empty view when unset/empty.
[[nodiscard]] inline std::string_view env_value(const char* name) noexcept {
    const char* const value = std::getenv(name);
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

/// One environment field: variable name, diagnostic field, apply hook.
template <class Settings> struct EnvField {
    const char* env_name;
    const char* field;
    void (*apply)(const char* env_name, const char* field,
                  std::vector<SettingsDiagnostic>& diagnostics, Settings& settings);
};

/// Apply every present (non-empty) environment field in table order.
template <class Settings, std::size_t N>
inline void apply_env_fields(const EnvField<Settings> (&fields)[N], Settings& settings) {
    for (const EnvField<Settings>& field : fields) {
        if (env_value(field.env_name).empty()) {
            continue;
        }
        field.apply(field.env_name, field.field, settings.diagnostics, settings);
    }
}

/// Nanosecond interval env override: unsigned parse with the int64 ceiling,
/// "expected a non-negative nanosecond count" on any failure (the
/// collect_interval pattern of every module).
inline void read_env_duration_ns(const char* env_name, const char* field,
                                 std::vector<SettingsDiagnostic>& diagnostics,
                                 std::chrono::nanoseconds& target) {
    std::uint64_t parsed = 0;
    if (parse_u64(env_value(env_name), parsed) &&
        parsed <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        target = std::chrono::nanoseconds{static_cast<std::int64_t>(parsed)};
    } else {
        add_diagnostic(diagnostics, SettingsSource::Environment, field,
                       "expected a non-negative nanosecond count");
    }
}

/// Core-mask env override applied onto a ThreadSpec (shared wording).
inline void read_env_core_mask(const char* env_name, const char* field,
                               std::vector<SettingsDiagnostic>& diagnostics, ThreadSpec& spec) {
    std::uint64_t parsed = 0;
    if (parse_u64(env_value(env_name), parsed)) {
        spec.set_cpus(CpuSet::from_core_mask(parsed));
    } else {
        add_diagnostic(diagnostics, SettingsSource::Environment, field,
                       "expected a non-negative integer mask");
    }
}

/// Plain unsigned env override ("expected a non-negative integer").
inline void read_env_plain_u64(const char* env_name, const char* field,
                               std::vector<SettingsDiagnostic>& diagnostics,
                               std::uint64_t& target) {
    std::uint64_t parsed = 0;
    if (parse_u64(env_value(env_name), parsed)) {
        target = parsed;
    } else {
        add_diagnostic(diagnostics, SettingsSource::Environment, field,
                       "expected a non-negative integer");
    }
}

/// Checked-range unsigned env override with a bespoke wording (the
/// max_tracers 1..64 pattern).
inline void read_env_range_u32(const char* env_name, const char* field, std::uint64_t lo,
                               std::uint64_t hi, std::vector<SettingsDiagnostic>& diagnostics,
                               std::uint32_t& target, const char* message) {
    std::uint64_t parsed = 0;
    if (parse_u64(env_value(env_name), parsed) && parsed >= lo && parsed <= hi) {
        target = static_cast<std::uint32_t>(parsed);
    } else {
        add_diagnostic(diagnostics, SettingsSource::Environment, field, message);
    }
}

} // namespace nexigen::core::settings_io
