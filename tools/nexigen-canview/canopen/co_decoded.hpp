// nexigen-canview — decoded CANopen presentation values.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

#include "canopen_types.hpp"
#include "eds_dictionary.hpp"

namespace nexigen::canview {

struct NodeBinding;

/// @brief Outcome of decoding one CANopen frame.
enum class CoDecodeStatus : std::uint8_t {
    Invalid,       ///< Not a CANopen frame.
    Ok,            ///< Fully decoded.
    Partial,       ///< Some signals or payload bytes are missing.
    UnknownObject, ///< Mapping references an object absent from the EDS.
    StaleMapping,  ///< Mapping epoch was evicted; nearest snapshot used.
    Aborted,       ///< SDO abort.
    TooLarge,      ///< Segmented transfer exceeded the assembly buffer.
    Unavailable,   ///< No binding/dictionary.
};

/// @brief How node colours are chosen.
enum class NodeColourMode : std::uint8_t {
    Node,       ///< Binding colour.
    Criticality ///< Errors/EMCY override the node colour.
};

/// @brief One decoded PDO signal.
struct CoSignal {
    const char* name{nullptr};              ///< EDS object name, or a synthetic label.
    std::array<char, 40> name_storage{};    ///< Storage for a synthetic label.
    EdsDataKind kind{EdsDataKind::Unknown}; ///< Value interpretation.
    bool valid{false};                      ///< Whether the value fits the payload.
    std::uint8_t bits{0};                   ///< Signal width.
    std::uint64_t raw{0};                   ///< Unsigned raw value.
    std::int64_t signed_value{0};           ///< Signed interpretation.
    double real{0.0};                       ///< REAL32/REAL64 interpretation.
    std::array<char, 40> text{};            ///< Rendered value.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line signal summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Decoded CANopen frame shared by the table, Inspector and export.
struct CoDecoded {
    CoService service{CoService::Unknown};          ///< Decoded service.
    CoDecodeStatus status{CoDecodeStatus::Invalid}; ///< Decode outcome.
    std::uint16_t slot{0xFFFFU};                    ///< Registry slot.
    std::uint8_t node_id{0};                        ///< Bound node id.
    std::uint16_t index{0};                         ///< SDO/object index.
    std::uint8_t subindex{0};                       ///< SDO/object subindex.
    std::uint32_t abort_code{0};                    ///< SDO abort code.
    bool error{false};                              ///< Error/EMCY/abort row.
    bool mapping_stale{false};                      ///< Mapping snapshot was evicted.
    std::uint8_t map_epoch{0};                      ///< Mapping epoch used.
    NmtState nmt_state{NmtState::Unknown};          ///< Observed NMT state.
    std::array<CoSignal, 16> signals{};             ///< Decoded signals.
    std::uint8_t signal_count{0};                   ///< Valid signal entries.
    std::array<char, 192> text{};                   ///< Human-readable summary.

    /// @brief Add one decoded signal when capacity allows.
    /// @param signal Signal to append.
    /// @return `true` when appended.
    bool add_signal(const CoSignal& signal) noexcept;
    /// @brief Set the summary text.
    /// @param value Text to copy, truncated to the buffer.
    void set_text(std::string_view value) noexcept;
    /// @brief Clear every field.
    void reset() noexcept { *this = CoDecoded{}; }

    /// @brief Cold-path diagnostic rendering.
    /// @return Summary text.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Spec name for one decoded PDO signal.
using PdoSignal = CoSignal;

/// @brief Write a decoded signal as JSON.
void to_json(nlohmann::json& j, const CoSignal& signal);
/// @brief Read a decoded signal from JSON without throwing.
void from_json(const nlohmann::json& j, CoSignal& signal) noexcept;
/// @brief Write a decoded record as JSON.
void to_json(nlohmann::json& j, const CoDecoded& decoded);
/// @brief Read a decoded record from JSON without throwing.
void from_json(const nlohmann::json& j, CoDecoded& decoded) noexcept;

/// @brief Colour for one decoded row.
/// @param decoded Decoded frame.
/// @param binding Bound node, or `nullptr`.
/// @param mode Colour policy.
/// @return RGBA colour.
[[nodiscard]] std::uint32_t co_colour(const CoDecoded& decoded, const NodeBinding* binding,
                                      NodeColourMode mode) noexcept;

/// @brief Inspector text for one decoded record.
/// @param decoded Decoded frame.
/// @return Multi-line object/service/signal summary.
[[nodiscard]] std::string co_inspector_text(const CoDecoded& decoded);

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `CoDecodeStatus`.
template <> struct EnumTraits<nexigen::canview::CoDecodeStatus> {
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::CoDecodeStatus value; ///< Enumerator.
        std::string_view name;                  ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 8> entries{{
        {nexigen::canview::CoDecodeStatus::Invalid, "Invalid"},
        {nexigen::canview::CoDecodeStatus::Ok, "Ok"},
        {nexigen::canview::CoDecodeStatus::Partial, "Partial"},
        {nexigen::canview::CoDecodeStatus::UnknownObject, "UnknownObject"},
        {nexigen::canview::CoDecodeStatus::StaleMapping, "StaleMapping"},
        {nexigen::canview::CoDecodeStatus::Aborted, "Aborted"},
        {nexigen::canview::CoDecodeStatus::TooLarge, "TooLarge"},
        {nexigen::canview::CoDecodeStatus::Unavailable, "Unavailable"},
    }};
};

/// @brief Vocabulary table for `NodeColourMode`.
template <> struct EnumTraits<nexigen::canview::NodeColourMode> {
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::NodeColourMode value; ///< Enumerator.
        std::string_view name;                  ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::canview::NodeColourMode::Node, "Node"},
        {nexigen::canview::NodeColourMode::Criticality, "Criticality"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical decode-status name.
[[nodiscard]] constexpr std::string_view to_string(CoDecodeStatus value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse a decode status.
[[nodiscard]] constexpr bool from_string(std::string_view name, CoDecodeStatus& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write a decode status as JSON.
inline void to_json(nlohmann::json& j, const CoDecodeStatus value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read a decode status without throwing.
inline void from_json(const nlohmann::json& j, CoDecodeStatus& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

/// @brief Canonical colour-mode name.
[[nodiscard]] constexpr std::string_view to_string(NodeColourMode value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse a colour mode.
[[nodiscard]] constexpr bool from_string(std::string_view name, NodeColourMode& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write a colour mode as JSON.
inline void to_json(nlohmann::json& j, const NodeColourMode value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read a colour mode without throwing.
inline void from_json(const nlohmann::json& j, NodeColourMode& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::canview
