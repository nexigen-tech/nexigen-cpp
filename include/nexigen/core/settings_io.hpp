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

/// @brief File-read outcome for optional settings sources.
///
/// The tri-state the per-module loaders carried: an absent source is not an
/// error, an unreadable one is.
///
/// @note Spec `core/sink-plumbing`, requirement "Shared settings loader
///       vocabulary" (one `FileRead` implementation for all loaders).
enum class FileRead : std::uint8_t {
    Absent, ///< Path empty or file does not exist; source disabled.
    Ok,     ///< Whole file read into the destination string.
    Failed  ///< File exists but could not be opened or read.
};

/// @brief Read a whole text file into a string.
///
/// @param path File path; an empty path reports `Absent` without a syscall.
/// @param out Destination resized to the file; empty on `Absent`/`Failed`.
/// @return `Absent` for an empty path or a missing file, `Ok` after a complete
///         read, `Failed` for any other open/read error.
/// @note Cold path only: allocates the destination and copies the path.
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

/// @brief Numeric contract part 1: accept an unsigned JSON number.
///
/// @param value JSON value to inspect.
/// @param out Destination assigned only for an unsigned number.
/// @return `true` when `value` is a JSON unsigned number.
[[nodiscard]] inline bool unsigned_value(const nlohmann::json& value, std::uint64_t& out) noexcept {
    if (!value.is_number_unsigned()) {
        return false;
    }
    out = value.get<std::uint64_t>();
    return true;
}

/// @brief Numeric contract part 2: parse a full-token decimal digit string.
///
/// @param text Candidate text; must be non-empty and entirely decimal digits.
/// @param out Destination assigned only on success.
/// @return `true` when the whole view parsed as a `std::uint64_t`.
[[nodiscard]] inline bool parse_u64(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

/// @brief Append one merge-layer finding to a settings diagnostics vector.
///
/// @param diagnostics Vector receiving the finding (the one every loader
///        carries).
/// @param source Merge layer that found it.
/// @param field Offending field name.
/// @param message Human-readable finding; moved into the diagnostic.
/// @note Spec `core/settings-vocabulary`, requirement "Settings diagnostic
///       records a finding". Cold path only.
inline void add_diagnostic(std::vector<SettingsDiagnostic>& diagnostics, SettingsSource source,
                           std::string field, std::string message) {
    diagnostics.push_back(SettingsDiagnostic{source, std::move(field), std::move(message)});
}

// --- loader vocabulary (dedup-sink-plumbing): the per-key read helpers the
// three module loaders carried as verbatim copies. Diagnostic texts are
// byte-identical to the per-module originals; an absent key leaves the
// target untouched and produces no diagnostic, an invalid value reports
// once and leaves the previous target intact.

/// @brief Read a JSON key into a `std::uint64_t` when present and in range.
///
/// An absent key leaves the target untouched and produces no diagnostic; an
/// invalid value reports once and leaves the previous target intact.
///
/// @param document JSON object to probe.
/// @param key Field name to look up.
/// @param max_value Inclusive upper bound for the accepted value.
/// @param target Destination assigned only for a valid value.
/// @param diagnostics Vector receiving a finding on rejection.
/// @param source Merge layer attributed to a finding.
/// @note Spec `core/sink-plumbing`, requirement "Shared settings loader
///       vocabulary" (diagnostics byte-identical to the module originals).
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

/// @brief Build the `sink.<key>` field name used in sink diagnostics.
///
/// @param key Sink-relative key.
/// @return Newly allocated `"sink.<key>"` text.
/// @note Spec `core/sink-plumbing`, requirement "Shared settings loader
///       vocabulary".
[[nodiscard]] inline std::string sink_field(const char* key) {
    return std::string("sink.") + key;
}

/// @brief Read an optional string key out of the sink object.
///
/// @param sink Sink JSON object supplied by `find_sink`.
/// @param key Sink-relative field name.
/// @param target Destination, untouched when the key is absent.
/// @param diagnostics Vector receiving a finding for a non-string value.
/// @param source Merge layer attributed to a finding.
/// @note Spec `core/sink-plumbing`, requirement "Shared settings loader
///       vocabulary" (`expected a string` wording).
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

/// @brief Read an optional boolean key out of the sink object.
///
/// @param sink Sink JSON object supplied by `find_sink`.
/// @param key Sink-relative field name.
/// @param target Destination, untouched when the key is absent.
/// @param diagnostics Vector receiving a finding for a non-boolean value.
/// @param source Merge layer attributed to a finding.
/// @note Spec `core/sink-plumbing`, requirement "Shared settings loader
///       vocabulary" (`expected a boolean` wording).
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

/// @brief Read an optional unsigned key out of the sink object.
///
/// @param sink Sink JSON object supplied by `find_sink`.
/// @param key Sink-relative field name.
/// @param target Destination, untouched when the key is absent.
/// @param diagnostics Vector receiving a finding for a non-unsigned value.
/// @param source Merge layer attributed to a finding.
/// @note Spec `core/sink-plumbing`, requirement "Shared settings loader
///       vocabulary" (`expected a non-negative integer` wording).
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

/// @brief Locate the optional `sink` object.
///
/// @param document JSON object to probe.
/// @param diagnostics Vector receiving a finding for a non-object `sink`.
/// @param source Merge layer attributed to a finding.
/// @return Pointer to the sink object, or `nullptr` when absent or invalid (an
///         invalid value is reported once and leaves previous settings intact).
/// @note Spec `core/sink-plumbing`, requirement "Shared settings loader
///       vocabulary" (`expected a JSON object` wording).
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

/// @brief One document field: the key names it in JSON, the hook reads it.
///
/// @tparam Settings Settings type the hook fills and the table merges into.
/// @note Spec `core/sink-plumbing`, requirement "Shared settings loader
///       vocabulary": table order is read order, preserving diagnostic order.
template <class Settings> struct ReadField {
    /// @brief JSON key naming the field.
    const char* key;
    /// @brief Hook invoked as `read(key, document, source, diagnostics,
    ///        settings)`.
    void (*read)(const char* key, const nlohmann::json& document, SettingsSource source,
                 std::vector<SettingsDiagnostic>& diagnostics, Settings& settings);
};

/// @brief One field of the sink sub-object.
///
/// Same contract as `ReadField`, but the hook receives the object handed over by
/// `find_sink`.
///
/// @tparam Settings Settings type the hook fills and the table merges into.
template <class Settings> struct SinkReadField {
    /// @brief Sink-relative JSON key naming the field.
    const char* key;
    /// @brief Hook invoked as `read(key, sink, source, diagnostics, settings)`.
    void (*read)(const char* key, const nlohmann::json& sink, SettingsSource source,
                 std::vector<SettingsDiagnostic>& diagnostics, Settings& settings);
};

/// @brief One JSON write slot: the key names it, the hook fills it.
///
/// @tparam Settings Settings type the hook reads.
/// @note Spec `core/sink-plumbing`, requirement "Shared settings loader
///       vocabulary": table order is write order, preserving JSON key order.
template <class Settings> struct WriteField {
    /// @brief JSON key naming the field.
    const char* key;
    /// @brief Hook invoked as `write(object, key, settings)`.
    void (*write)(nlohmann::json& object, const char* key, const Settings& settings);
};

/// @brief Read a nanosecond field bounded by the `std::int64_t` ceiling.
///
/// Staged through `std::uint64_t` and committed only when no diagnostic was
/// appended (the `collect_interval` pattern).
///
/// @param document JSON object to probe.
/// @param key Field name to look up.
/// @param source Merge layer attributed to a finding.
/// @param diagnostics Vector receiving a finding on rejection.
/// @param target Duration, replaced only when the field parses in range.
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

/// @brief Read the legacy `core_mask` key into a `ThreadSpec`.
///
/// Parsed with the uint vocabulary; the spec applies the mask whenever the key
/// is PRESENT, even if invalid (the mask then stays 0, matching the historical
/// merge order).
///
/// @param document JSON object to probe.
/// @param key Field name to look up.
/// @param source Merge layer attributed to a finding.
/// @param diagnostics Vector receiving a finding on rejection.
/// @param spec Spec whose CPU set is replaced when the key is present.
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

/// @brief Read a full `worker_thread` object through the shared vocabulary.
///
/// @param document JSON object to probe.
/// @param key Field name holding the worker object.
/// @param source Merge layer attributed to a finding.
/// @param diagnostics Vector receiving a finding for an invalid spec.
/// @param spec Destination replaced only when the object parses fully.
/// @note Invalid `name`/`cpus`/`policy`/`priority`/`mode` fields report one
///       finding and leave the previous spec intact.
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

/// @brief Read a `std::uint32_t` field staged through `std::uint64_t`.
///
/// Committed only when no diagnostic was appended (the `max_*` counters
/// pattern).
///
/// @param document JSON object to probe.
/// @param key Field name to look up.
/// @param source Merge layer attributed to a finding.
/// @param diagnostics Vector receiving a finding on rejection.
/// @param target Value, replaced only for an in-range field.
inline void read_bounded_u32(const nlohmann::json& document, const char* key, SettingsSource source,
                             std::vector<SettingsDiagnostic>& diagnostics, std::uint32_t& target) {
    std::uint64_t value = target;
    const std::size_t before = diagnostics.size();
    read_uint(document, key, std::numeric_limits<std::uint32_t>::max(), value, diagnostics, source);
    if (diagnostics.size() == before) {
        target = static_cast<std::uint32_t>(value);
    }
}

/// @brief Read a `std::uint32_t` field staged through `std::uint64_t`.
///
/// Assigned unconditionally: an invalid read leaves the staged copy of the old
/// value, so the net effect is a no-op (the `worker_poll_interval_us` pattern).
///
/// @param document JSON object to probe.
/// @param key Field name to look up.
/// @param max_value Inclusive upper bound for the accepted value.
/// @param source Merge layer attributed to a finding.
/// @param diagnostics Vector receiving a finding on rejection.
/// @param target Value, rewritten from the staged copy on every call.
inline void read_staged_u32(const nlohmann::json& document, const char* key,
                            std::uint64_t max_value, SettingsSource source,
                            std::vector<SettingsDiagnostic>& diagnostics, std::uint32_t& target) {
    std::uint64_t value = target;
    read_uint(document, key, max_value, value, diagnostics, source);
    target = static_cast<std::uint32_t>(value);
}

/// @brief Read a `std::uint64_t` field assigned unconditionally.
///
/// An invalid read keeps the staged copy of the old value, so the net effect is
/// a no-op (the `max_spans`/`window_ns` pattern).
///
/// @param document JSON object to probe.
/// @param key Field name to look up.
/// @param max_value Inclusive upper bound for the accepted value.
/// @param source Merge layer attributed to a finding.
/// @param diagnostics Vector receiving a finding on rejection.
/// @param target Value, rewritten from the staged copy on every call.
inline void read_staged_u64(const nlohmann::json& document, const char* key,
                            std::uint64_t max_value, SettingsSource source,
                            std::vector<SettingsDiagnostic>& diagnostics, std::uint64_t& target) {
    std::uint64_t value = target;
    read_uint(document, key, max_value, value, diagnostics, source);
    target = value;
}

/// @brief Read an enum field inside the sink object.
///
/// Type-checks the string and parses it through the enum's own ADL
/// `from_string`; `allowed` supplies the historical rejection wording.
///
/// @tparam Enum Enum type providing the `from_string` vocabulary.
/// @param sink Sink JSON object supplied by `find_sink`.
/// @param key Sink-relative field name.
/// @param source Merge layer attributed to a finding.
/// @param diagnostics Vector receiving a finding on rejection.
/// @param target Enum, replaced only on a successful parse.
/// @param allowed Diagnostic text used for a wrong type or unknown name.
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

/// @brief Run a document table in order.
///
/// @tparam Settings Settings type merged into.
/// @tparam N Number of table rows.
/// @param document JSON object to read.
/// @param source Merge layer attributed to every finding.
/// @param fields Field table; table order is read order.
/// @param settings Destination carrying `diagnostics` and the target fields.
/// @note One driver per direction: per-module tables list `{key, hook}` and the
///       table order is the read order.
template <class Settings, std::size_t N>
inline void read_fields(const nlohmann::json& document, SettingsSource source,
                        const ReadField<Settings> (&fields)[N], Settings& settings) {
    for (const ReadField<Settings>& field : fields) {
        field.read(field.key, document, source, settings.diagnostics, settings);
    }
}

/// @brief Run a sink table: locate the sink once, then read every field.
///
/// A missing sink is silent; an invalid (non-object) sink produces the one
/// shared diagnostic and stops.
///
/// @tparam Settings Settings type merged into.
/// @tparam N Number of table rows.
/// @param document JSON object to read.
/// @param source Merge layer attributed to every finding.
/// @param fields Sink field table; table order is read order.
/// @param settings Destination carrying `diagnostics` and the target fields.
/// @note Spec `core/sink-plumbing`, requirement "Shared settings loader
///       vocabulary".
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

/// @brief Run a write table in order.
///
/// @tparam Settings Settings type read by the hooks.
/// @tparam N Number of table rows.
/// @param object JSON object filled in place.
/// @param fields Write table; table order is JSON key order.
/// @param settings Source settings rendered into `object`.
template <class Settings, std::size_t N>
inline void write_fields(nlohmann::json& object, const WriteField<Settings> (&fields)[N],
                         const Settings& settings) {
    for (const WriteField<Settings>& field : fields) {
        field.write(object, field.key, settings);
    }
}

/// @brief Append the shared `diagnostics` array to a settings JSON object.
///
/// @param object JSON object receiving the `diagnostics` member.
/// @param diagnostics Findings to serialize through the diagnostic ADL writer.
/// @note Shape shared by every module settings JSON (mandates 9/10).
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

/// @brief Render the shared `Settings(...)` summary line.
///
/// The modules pass their own `to_string` views; joining and quoting lives here
/// once.
///
/// @param global_loaded Whether the global file was read and merged.
/// @param local_loaded Whether the local file was read and merged.
/// @param options Rendered options view from the module.
/// @param sink Rendered sink view from the module.
/// @param diagnostics Findings joined with `; ` in report order.
/// @return Text such as `Settings(global_loaded=..., ..., diagnostics=[...])`.
/// @note Spec `core/settings-vocabulary`, requirement "Settings vocabulary is
///       cold path only"; mandate 9.
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

/// @brief The three-state settings loader skeleton.
///
/// Resets the destination, reads the global file, the local file, and the
/// environment overlay in that order; error precedence is `SystemError` over
/// `InvalidArgument`. Diagnostics are byte-identical to the per-module bodies
/// this replaces (same wordings, same order).
///
/// @tparam Settings Settings type carrying `global_loaded`/`local_loaded`/
///         `diagnostics` and whatever the merge hook fills.
/// @param paths Explicit global/local file pair; an empty path disables a layer.
/// @param out Destination reset and then merged into.
/// @param merge Module field-table merge applied per file layer.
/// @param apply_env Module environment-overlay applier.
/// @return `Status{}` when every present source was valid; `SystemError` when a
///         present file could not be read; `InvalidArgument` when a document was
///         malformed or a field was rejected (diagnostics retained either way).
/// @note Spec `core/settings-vocabulary`, requirement "Settings paths carry an
///       explicit global/local pair".
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

/// @brief Raw environment value, or an empty view when unset or empty.
///
/// @param name Environment variable name.
/// @return View over the process environment value; empty when unset.
[[nodiscard]] inline std::string_view env_value(const char* name) noexcept {
    const char* const value = std::getenv(name);
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

/// @brief One environment field: variable name, diagnostic field, hook.
///
/// @tparam Settings Settings type the hook merges into.
template <class Settings> struct EnvField {
    /// @brief Environment variable name to read.
    const char* env_name;
    /// @brief Diagnostic field name attributed to a finding.
    const char* field;
    /// @brief Hook invoked as `apply(env_name, field, diagnostics, settings)`.
    void (*apply)(const char* env_name, const char* field,
                  std::vector<SettingsDiagnostic>& diagnostics, Settings& settings);
};

/// @brief Apply every present (non-empty) environment field in table order.
///
/// @tparam Settings Settings type merged into.
/// @tparam N Number of table rows.
/// @param fields Environment field table.
/// @param settings Destination receiving the overrides and findings.
template <class Settings, std::size_t N>
inline void apply_env_fields(const EnvField<Settings> (&fields)[N], Settings& settings) {
    for (const EnvField<Settings>& field : fields) {
        if (env_value(field.env_name).empty()) {
            continue;
        }
        field.apply(field.env_name, field.field, settings.diagnostics, settings);
    }
}

/// @brief Nanosecond interval environment override.
///
/// Parses an unsigned value bounded by the `std::int64_t` ceiling and reports
/// `expected a non-negative nanosecond count` on any failure (the
/// `collect_interval` pattern of every module).
///
/// @param env_name Environment variable name.
/// @param field Diagnostic field name attributed to a finding.
/// @param diagnostics Vector receiving a finding on rejection.
/// @param target Duration, replaced only on a successful parse.
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

/// @brief Core-mask environment override applied onto a `ThreadSpec`.
///
/// @param env_name Environment variable name.
/// @param field Diagnostic field name attributed to a finding.
/// @param diagnostics Vector receiving a finding on rejection.
/// @param spec Spec whose CPU set is replaced on a successful parse.
/// @note Shared `expected a non-negative integer mask` wording.
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

/// @brief Plain unsigned environment override.
///
/// @param env_name Environment variable name.
/// @param field Diagnostic field name attributed to a finding.
/// @param diagnostics Vector receiving a finding on rejection.
/// @param target Value, replaced only on a successful parse.
/// @note Reports `expected a non-negative integer` on failure.
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

/// @brief Checked-range unsigned environment override.
///
/// @param env_name Environment variable name.
/// @param field Diagnostic field name attributed to a finding.
/// @param lo Inclusive lower bound.
/// @param hi Inclusive upper bound.
/// @param diagnostics Vector receiving a finding on rejection.
/// @param target Value, replaced only for an in-range parse.
/// @param message Bespoke diagnostic wording (the `max_tracers` 1..64 pattern).
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
