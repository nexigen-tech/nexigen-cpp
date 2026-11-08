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
#include <nexigen/core/settings_source.hpp>

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
// one driver per direction, per-module tables list {key, hook}. Table order =
// read/write order, so diagnostics order and JSON key order are preserved by
// construction. Hooks are plain function pointers over module-local free
// functions (no std::function heap); the diagnostics plumbing itself lives in
// the primitives above, so a hook body is only the field's own logic.

/// One document field: the key names it in JSON, the hook reads it.
template <class Settings> struct ReadField {
    const char* key;
    void (*read)(const nlohmann::json& document, SettingsSource source,
                 std::vector<SettingsDiagnostic>& diagnostics, Settings& settings);
};

/// One document field of the sink sub-object: same contract, `document` is
/// the sink object handed over by find_sink.
template <class Settings> struct SinkReadField {
    const char* key;
    void (*read)(const nlohmann::json& sink, SettingsSource source,
                 std::vector<SettingsDiagnostic>& diagnostics, Settings& settings);
};

/// Run the document table in order. Keys are consumed through the table's
/// own hooks (each hook locates its key via the shared primitives).
template <class Settings, std::size_t N>
inline void read_fields(const nlohmann::json& document, SettingsSource source,
                        const ReadField<Settings> (&fields)[N], Settings& settings) {
    for (const ReadField<Settings>& field : fields) {
        field.read(document, source, settings.diagnostics, settings);
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
        field.read(*sink, source, settings.diagnostics, settings);
    }
}

/// One JSON write slot: the key names it, the hook fills it.
template <class Settings> struct WriteField {
    const char* key;
    void (*write)(nlohmann::json& object, const Settings& settings);
};

/// Run the write table in order.
template <class Settings, std::size_t N>
inline void write_fields(nlohmann::json& object, const WriteField<Settings> (&fields)[N],
                         const Settings& settings) {
    for (const WriteField<Settings>& field : fields) {
        field.write(object, settings);
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

} // namespace nexigen::core::settings_io
