// nexigen::core — one diagnostics transition (change
// add-telemetry-diagnostics; spec core/diagnostics).
//
// Carries a source, a transition kind, and the applicable error code (`Ok` for
// non-error transitions). Cold-path only: providers build an event when a
// state changes, never per record or per batch.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/core/diagnostic_kind.hpp>
#include <nexigen/core/diagnostic_source.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::core {

/// @brief One diagnostics transition emitted by a telemetry subsystem.
///
/// Trivially copyable and pointer-free so it can cross the binding by reference
/// without allocation.
///
/// @note Spec `core/diagnostics`, requirement "Diagnostic vocabulary and
///       serialization".
struct DiagnosticEvent {
    DiagnosticSource source{DiagnosticSource::Chrono}; ///< Producing subsystem.
    DiagnosticKind kind{DiagnosticKind::Started};      ///< Transition kind.
    ErrorCode error{ErrorCode::Ok};                    ///< Error, or `Ok`.

    /// @brief Render one event for cold-path diagnostics.
    ///
    /// @return Text carrying source, kind, and error names.
    /// @note Spec `core/diagnostics`, requirement "Diagnostic vocabulary and
    ///       serialization"; mandate 9.
    [[nodiscard]] std::string to_string() const {
        std::string out = "DiagnosticEvent(source=";
        out += core::to_string(source);
        out += ", kind=";
        out += core::to_string(kind);
        out += ", error=";
        out += core::to_string(error);
        out += ")";
        return out;
    }
};

static_assert(std::is_trivially_copyable_v<DiagnosticEvent>);

/// @brief ADL JSON writer for one event.
///
/// @param j JSON object overwritten with `source`, `kind`, and `error` names.
/// @param event Event to serialize.
/// @note Cold path only; mandate 10.
inline void to_json(nlohmann::json& j, const DiagnosticEvent& event) {
    j = nlohmann::json{
        {"source", std::string(to_string(event.source))},
        {"kind", std::string(to_string(event.kind))},
        {"error", std::string(to_string(event.error))},
    };
}

/// @brief ADL JSON reader, non-throwing under `JSON_NOEXCEPTION=1`.
///
/// Each field is assigned only when present as a string and parseable; a
/// non-object input leaves the event unchanged.
///
/// @param j JSON value to read.
/// @param event Destination.
/// @note Cold path only; mandate 10.
inline void from_json(const nlohmann::json& j, DiagnosticEvent& event) noexcept {
    if (!j.is_object()) {
        return;
    }
    if (const auto source_it = j.find("source");
        source_it != j.end() && source_it->is_string()) {
        if (const auto* name = source_it->get_ptr<const nlohmann::json::string_t*>()) {
            DiagnosticSource parsed = event.source;
            if (from_string(*name, parsed)) {
                event.source = parsed;
            }
        }
    }
    if (const auto kind_it = j.find("kind"); kind_it != j.end() && kind_it->is_string()) {
        if (const auto* name = kind_it->get_ptr<const nlohmann::json::string_t*>()) {
            DiagnosticKind parsed = event.kind;
            if (from_string(*name, parsed)) {
                event.kind = parsed;
            }
        }
    }
    if (const auto error_it = j.find("error"); error_it != j.end() && error_it->is_string()) {
        if (const auto* name = error_it->get_ptr<const nlohmann::json::string_t*>()) {
            ErrorCode parsed = event.error;
            if (from_string(*name, parsed)) {
                event.error = parsed;
            }
        }
    }
}

/// @brief Free diagnostic helper mirroring the member.
///
/// @param event Event to render.
/// @return The same text the member returns.
/// @note Spec `core/diagnostics`, requirement "Diagnostic vocabulary and
///       serialization"; mandate 9.
[[nodiscard]] inline std::string to_string(const DiagnosticEvent& event) {
    return event.to_string();
}

} // namespace nexigen::core
