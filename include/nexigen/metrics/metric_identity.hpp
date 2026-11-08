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

/// Maximum length of a meter name, meter version or metric name (chars; the
/// stored buffer holds one more byte for the NUL).
inline constexpr std::size_t max_name_length = 63;

/// Maximum length of a state value (a current named state such as a mode or
/// enum spelling); the stored buffer holds one more byte for the NUL. Mirrors
/// max_name_length.
inline constexpr std::size_t max_state_length = 63;

/// Maximum number of buckets carried by a histogram snapshot.
inline constexpr std::size_t max_snapshot_buckets = 256;

namespace detail {

/// Copy a name into a fixed NUL-terminated buffer. Returns false when the
/// name is empty or longer than max_name_length (the caller maps that to
/// core::ErrorCode::InvalidArgument). Never allocates.
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

/// Copy a stored name back out as a view (stopping at the first NUL).
[[nodiscard]] inline std::string_view view_name(const char (&src)[max_name_length + 1]) noexcept {
    std::size_t len = 0;
    while (len < max_name_length + 1 && src[len] != '\0') {
        ++len;
    }
    return std::string_view(src, len);
}

/// Copy a stored name field verbatim (including its NUL).
inline void copy_field(char (&dst)[max_name_length + 1],
                       const char (&src)[max_name_length + 1]) noexcept {
    for (std::size_t i = 0; i <= max_name_length; ++i) {
        dst[i] = src[i];
    }
}

} // namespace detail

/// Who produced a record: the meter that owns the instrument.
struct MetricIdentity {
    char meter_name[max_name_length + 1]{};
    char meter_version[max_name_length + 1]{};
    char metric_name[max_name_length + 1]{};
    char unit[max_name_length + 1]{};

    [[nodiscard]] std::string to_string() const;
};

namespace detail {

/// Copy the meter part of an identity (name + version).
inline void copy_meter_identity(MetricIdentity& to, const MetricIdentity& from) noexcept {
    copy_field(to.meter_name, from.meter_name);
    copy_field(to.meter_version, from.meter_version);
}

} // namespace detail

static_assert(std::is_trivially_copyable_v<MetricIdentity>);

[[nodiscard]] std::string to_string(const MetricIdentity& identity);

void to_json(nlohmann::json& j, const MetricIdentity& identity);
void from_json(const nlohmann::json& j, MetricIdentity& out) noexcept;

} // namespace nexigen::metrics

/// MetricIdentity formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::MetricIdentity> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::MetricIdentity& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
