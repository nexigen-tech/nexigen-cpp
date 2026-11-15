// nexigen::integration simulation_suite — shared payload crossing the com
// transport (change add-simulation-suite-example;
// spec examples/simulation-suite "Vector payload with canonical conversions").
//
// VectorSample is the fixed, trivially-copyable value the generator publishes
// and both receivers consume: a monotonic index, the send timestamp the
// receiver subtracts to measure latency, and ten values. No pointers, virtual
// tables, or owning members cross shared memory, so the zero-copy transport
// treats the value as plain bytes.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/core/detail/json_field.hpp>

namespace simulation_suite {

/// @brief Number of values carried by one vector.
///
/// Fixed so the payload stays a compile-time constant size and the receivers
/// can print it without allocating.
inline constexpr std::size_t k_vector_values = 10;

/// @brief One fixed payload published once per period.
///
/// @note Spec `examples/simulation-suite`, requirement "Vector payload with
///       canonical conversions".
struct VectorSample {
    std::uint64_t sequence{0};                ///< Monotonic vector index.
    std::uint64_t sent_ns{0};                 ///< Send instant, `steady_clock` nanoseconds.
    double values[k_vector_values]{};         ///< Ten generated values.

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Text of the form `VectorSample(sequence=..., sent_ns=...,
    ///         values=[a b ...])`; allocates, so cold path only.
    /// @note Mandate 9 (universal `to_string`). Spec
    ///       `examples/simulation-suite`, requirement "Vector payload with
    ///       canonical conversions".
    [[nodiscard]] std::string to_string() const {
        std::string rendered = std::format("VectorSample(sequence={}, sent_ns={}, values=[",
                                           sequence, sent_ns);
        for (std::size_t i = 0; i < k_vector_values; ++i) {
            if (i != 0U) {
                rendered.push_back(' ');
            }
            rendered += std::format("{:.3f}", values[i]);
        }
        rendered += "])";
        return rendered;
    }
};

/// @brief Compile-time proof that the payload may cross the transport.
static_assert(std::is_trivially_copyable_v<VectorSample>,
              "VectorSample must be trivially copyable to cross the com transport");

/// @brief Compile-time proof of the documented message size.
static_assert(sizeof(VectorSample) == 96,
              "VectorSample is 8 + 8 + 10 * 8 bytes on the target ABI");

/// @brief Free helper mirroring `VectorSample::to_string()` (mandate 9).
///
/// @param sample Sample to render.
/// @return The sample's diagnostic text.
[[nodiscard]] inline std::string to_string(const VectorSample& sample) {
    return sample.to_string();
}

/// @brief ADL JSON writer for a vector sample.
///
/// @param j Destination JSON value; overwritten with an object.
/// @param sample Sample to encode.
/// @note Mandate 10 (ADL JSON pair); cold path only. Spec
///       `examples/simulation-suite`, requirement "Vector payload with
///       canonical conversions".
inline void to_json(nlohmann::json& j, const VectorSample& sample) {
    nlohmann::json values = nlohmann::json::array();
    for (std::size_t i = 0; i < k_vector_values; ++i) {
        values.push_back(sample.values[i]);
    }
    j = nlohmann::json{{"sequence", sample.sequence},
                       {"sent_ns", sample.sent_ns},
                       {"values", std::move(values)}};
}

/// @brief ADL JSON reader for a vector sample.
///
/// Strictly non-throwing: a non-object, a missing field, a wrong type, or a
/// values array of the wrong length leaves `sample` unchanged.
///
/// @param j Source JSON value; a non-object is ignored.
/// @param sample Receives the parsed fields; unchanged on malformed input.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec
///       `examples/simulation-suite`, requirement "Vector payload with
///       canonical conversions".
inline void from_json(const nlohmann::json& j, VectorSample& sample) noexcept {
    if (!j.is_object()) {
        return;
    }
    std::uint64_t sequence = 0;
    std::uint64_t sent_ns = 0;
    if (!core::detail::read_unsigned(j, "sequence", sequence)) {
        return;
    }
    if (!core::detail::read_unsigned(j, "sent_ns", sent_ns)) {
        return;
    }
    const auto values_it = j.find("values");
    if (values_it == j.end() || !values_it->is_array() ||
        values_it->size() != k_vector_values) {
        return;
    }
    std::array<double, k_vector_values> parsed{};
    for (std::size_t i = 0; i < k_vector_values; ++i) {
        const nlohmann::json& value = (*values_it)[i];
        if (!value.is_number()) {
            return;
        }
        parsed[i] = value.get<double>();
    }
    sample.sequence = sequence;
    sample.sent_ns = sent_ns;
    for (std::size_t i = 0; i < k_vector_values; ++i) {
        sample.values[i] = parsed[i];
    }
}

} // namespace simulation_suite

/// @brief `std::formatter` for `VectorSample` (project mandate 11).
///
/// Delegates to the member diagnostic, so `std::format("{}", sample)` equals
/// `to_string(sample)`.
template <> struct std::formatter<simulation_suite::VectorSample> : std::formatter<std::string_view> {
    /// @brief Format one sample as its diagnostic string.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param sample Sample to render.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx>
    auto format(const simulation_suite::VectorSample& sample, Ctx& ctx) const {
        const std::string text = sample.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
