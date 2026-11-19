// nexigen::com — optional FPGA metadata for an event.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::com {

/// @brief Optional hardware metadata supplied by a gateway.
///
/// The sequence is trace-only. The event instant is the raw device counter plus
/// the time-base domain id and epoch; drivers never convert ticks to
/// nanoseconds. `canonical_ns` is an optional projection the gateway may derive
/// from a domain map it holds.
///
/// @note Spec `com/event-system`, requirement "Hardware gateway ingress".
struct HardwareMetadata {
    std::uint64_t sequence{0};     ///< Raw FPGA sequence (trace-only).
    std::uint64_t native_ticks{0}; ///< Raw device counter, unchanged.
    std::uint64_t domain_epoch{0}; ///< Epoch of the native domain.
    std::uint64_t canonical_ns{0}; ///< Optional canonical projection.
    std::uint16_t domain_id{0};    ///< Time-base domain id.
    std::uint32_t event_code{0};   ///< Raw FPGA event code.
    bool has_sequence{false};      ///< Enables `sequence`.
    bool has_ticks{false};         ///< Enables `native_ticks`.
    bool has_event_code{false};    ///< Enables `event_code`.
    bool has_canonical{false};     ///< Enables `canonical_ns`.

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable metadata summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `HardwareMetadata::to_string`.
/// @param metadata Metadata to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const HardwareMetadata& metadata) {
    return metadata.to_string();
}

/// @brief Serialize hardware metadata to JSON.
/// @param j Destination JSON object.
/// @param metadata Metadata to serialize.
void to_json(nlohmann::json& j, const HardwareMetadata& metadata);

/// @brief Parse hardware metadata from JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param metadata Destination metadata.
void from_json(const nlohmann::json& j, HardwareMetadata& metadata) noexcept;

} // namespace nexigen::com

/// @brief Formats hardware metadata as its diagnostic string.
template <>
struct std::formatter<nexigen::com::HardwareMetadata> : std::formatter<std::string_view> {
    /// @brief Write the metadata diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param metadata Metadata to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::HardwareMetadata& metadata, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(metadata.to_string(), ctx);
    }
};
