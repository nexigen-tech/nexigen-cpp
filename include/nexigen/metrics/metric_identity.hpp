// nexigen::metrics — who produced a record: the owning meter (design D12;
// spec metrics/metric-provider "Record and batch snapshot serialization").
#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace nexigen::metrics {

/// @brief Maximum length of a meter name, meter version or metric name, in
///        characters (the stored buffer holds one more byte for the NUL).
///
/// @note Spec `metrics/metric-provider`, requirement "Pre-allocated aligned
///       metric registry"; longer names are rejected with
///       `core::ErrorCode::InvalidArgument`.
inline constexpr std::size_t max_name_length = 63;

/// @brief Maximum length of a state value, in characters (the stored buffer
///        holds one more byte for the NUL).
///
/// Covers a current named state such as a mode or an enum spelling; mirrors
/// `max_name_length`.
///
/// @note Spec `metrics/state-gauge`, requirement "State recording surface".
inline constexpr std::size_t max_state_length = 63;

/// @brief Maximum number of buckets carried by a histogram snapshot.
///
/// @note Spec `metrics/snapshot-format`, requirement "Statistics and histogram
///       snapshots use typed numeric readers"; array reads are capped at this
///       bound.
inline constexpr std::size_t max_snapshot_buckets = 256;

namespace detail {

/// @brief Copy a name into a fixed NUL-terminated buffer.
///
/// Never allocates and never partially writes an unacceptable name.
///
/// @param dst Destination buffer of `max_name_length + 1` bytes.
/// @param name Source view; must be non-empty and at most `max_name_length`.
/// @return `true` on success; `false` when `name` is empty or too long, leaving
///         `dst` untouched. Callers map `false` to
///         `core::ErrorCode::InvalidArgument`.
/// @note Spec `metrics/metric-provider`, requirement "Pre-allocated aligned
///       metric registry".
[[nodiscard]] inline bool copy_name(char (&dst)[max_name_length + 1],
                                    std::string_view name) noexcept {
    if (name.empty() || name.size() > max_name_length) {
        return false;
    }
    for (std::size_t i = 0; i < name.size(); ++i) {
        dst[i] = name[i];
    }
    dst[name.size()] = '\0';
    return true;
}

/// @brief Copy a stored name back out as a view, stopping at the first NUL.
///
/// @param src Fixed buffer previously filled by `copy_name`.
/// @return View into `src`; empty when the first byte is NUL. Never allocates.
[[nodiscard]] inline std::string_view view_name(const char (&src)[max_name_length + 1]) noexcept {
    std::size_t len = 0;
    while (len < max_name_length + 1 && src[len] != '\0') {
        ++len;
    }
    return std::string_view(src, len);
}

/// @brief Copy a stored name field verbatim, including its terminating NUL.
///
/// @param dst Destination buffer of `max_name_length + 1` bytes.
/// @param src Source buffer of the same size.
inline void copy_field(char (&dst)[max_name_length + 1],
                       const char (&src)[max_name_length + 1]) noexcept {
    for (std::size_t i = 0; i <= max_name_length; ++i) {
        dst[i] = src[i];
    }
}

} // namespace detail

/// @brief Who produced a record: the meter that owns the instrument.
///
/// All four names are inline bounded buffers (`max_name_length` characters plus
/// NUL), so the identity stays fixed-size and trivially copyable for the
/// cross-process frame. The provider fills it at creation time; harvesting only
/// copies it.
///
/// @note Spec `metrics/metric-provider`, requirement "Factory hierarchy and
///       passkey construction" (meter name and version are retained).
/// @note Spec `metrics/snapshot-format`, requirement "Identity and scalar
///       snapshots round-trip safely".
struct MetricIdentity {
    char meter_name[max_name_length + 1]{};    ///< Owning meter's name.
    char meter_version[max_name_length + 1]{}; ///< Owning meter's version.
    char metric_name[max_name_length + 1]{};   ///< Instrument name within the meter.
    char unit[max_name_length + 1]{};          ///< Declared unit, or empty.

    /// @brief Render the identity as a cold-path diagnostic string.
    ///
    /// @return Human-readable text carrying the meter, metric, and unit names.
    /// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
    ///       strings"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

namespace detail {

/// @brief Copy the meter part (name and version) of an identity.
///
/// Leaves the metric name and unit of `to` untouched.
///
/// @param to Destination identity, partially overwritten.
/// @param from Source identity.
inline void copy_meter_identity(MetricIdentity& to, const MetricIdentity& from) noexcept {
    copy_field(to.meter_name, from.meter_name);
    copy_field(to.meter_version, from.meter_version);
}

} // namespace detail

static_assert(std::is_trivially_copyable_v<MetricIdentity>);

/// @brief Free diagnostic helper mirroring `MetricIdentity::to_string()`.
///
/// @param identity Identity to render.
/// @return The same text the member returns.
/// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
///       strings"; diagnostic only (project mandate 9).
[[nodiscard]] std::string to_string(const MetricIdentity& identity);

/// @brief Write a metric identity as JSON (cold path).
///
/// @param j Output JSON object with the meter, metric, and unit fields.
/// @param identity Identity to serialize.
/// @note Spec `metrics/snapshot-format`, requirement "Identity and scalar
///       snapshots round-trip safely"; JSON only (project mandate 10).
void to_json(nlohmann::json& j, const MetricIdentity& identity);
/// @brief Parse a metric identity from JSON without throwing.
///
/// Copies each string field through `detail::copy_name`, which accepts only a
/// non-empty value of at most `max_name_length` bytes; missing, non-string,
/// empty, or over-long fields leave that destination field untouched.
///
/// @param j JSON object to read.
/// @param out Destination, fields updated independently.
/// @note Spec `metrics/snapshot-format`, requirement "Identity and scalar
///       snapshots round-trip safely".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, MetricIdentity& out) noexcept;

} // namespace nexigen::metrics

/// @brief `std::formatter` for `MetricIdentity`, rendering its diagnostic text.
///
/// @note Project mandate 11; formatting is cold path only.
template <>
struct std::formatter<nexigen::metrics::MetricIdentity> : std::formatter<std::string_view> {
    /// @brief Format the identity through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param value Identity to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::metrics::MetricIdentity& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
