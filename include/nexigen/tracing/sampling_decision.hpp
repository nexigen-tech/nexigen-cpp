// nexigen::tracing — SamplingDecision enum.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::tracing {

/// @brief The complete sampling vocabulary; there are no policy engines.
///
/// A decision is made once per span at start: `Drop` short-circuits span
/// recording, `RecordAndSample` records it. The enum follows the tracing-wide
/// enum contract for names, JSON, and formatting, and its values are append-only
/// byte-sized enumerators.
///
/// @note Spec `tracing/span-enums`, requirement "Span enum catalogue".
enum class SamplingDecision : std::uint8_t {
    Drop,            ///< Do not record: start_span short-circuits.
    RecordAndSample, ///< Record the span (default).
};

} // namespace nexigen::tracing

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief `core::enum_name`/`enum_value` vocabulary for `SamplingDecision`.
///
/// The table is the single source of the canonical spellings used by both name
/// directions.
template <> struct EnumTraits<nexigen::tracing::SamplingDecision> {
    /// @brief One enumerator paired with its canonical name.
    struct Entry {
        nexigen::tracing::SamplingDecision value; ///< Enumerator.
        std::string_view name;                    ///< Exact canonical spelling.
    };
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::tracing::SamplingDecision::Drop, "Drop"},
        {nexigen::tracing::SamplingDecision::RecordAndSample, "RecordAndSample"},
    }};
};

} // namespace nexigen::core

namespace nexigen::tracing {

/// @brief Render the canonical name of a sampling decision.
///
/// @param decision Decision to render.
/// @return `"Drop"` or `"RecordAndSample"`, or `"Unknown"` for an out-of-range
///         cast; never throws or allocates.
/// @note Spec `tracing/span-enums`, requirement "Constexpr span enum names";
///       project mandate 9 (`to_string`).
[[nodiscard]] constexpr std::string_view to_string(SamplingDecision decision) noexcept {
    return core::enum_name(decision);
}

/// @brief Parse a canonical sampling-decision name.
///
/// @param name Name to parse; comparison is case-sensitive.
/// @param out Destination, written only on success.
/// @return `true` when `name` matched a canonical spelling; on `false` the
///         out-parameter is left unchanged.
/// @note Spec `tracing/span-enums`, requirement "Constexpr span enum names".
[[nodiscard]] constexpr bool from_string(std::string_view name, SamplingDecision& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Parse the decision from JSON without throwing.
///
/// A non-string or unrecognized value leaves `out` unchanged; the call never
/// throws or aborts under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON value to read.
/// @param out Destination, written only on a recognized string.
/// @note Spec `tracing/span-enums`, requirement "Non-throwing span enum JSON
///       serialization"; project mandate 10 (JSON).
inline void from_json(const nlohmann::json& j, SamplingDecision& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

/// @brief Serialize the decision as its canonical name string.
///
/// @param j Destination JSON value, overwritten with the name.
/// @param samplingDecision Decision to serialize.
/// @note Spec `tracing/span-enums`, requirement "Non-throwing span enum JSON
///       serialization"; project mandate 10 (JSON).
inline void to_json(nlohmann::json& j, const SamplingDecision samplingDecision) {
    core::mandate::enum_to_json(j, samplingDecision);
}

} // namespace nexigen::tracing
