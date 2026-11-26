// nexigen-canview — canonical session JSONL v1 contract.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>
#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_message.hpp>

#include "gap_reason.hpp"
#include "record_kind.hpp"
#include "view_channel.hpp"

namespace nexigen::canview {

/// @brief Typed JSONL record kind.
enum class CanViewRecordType : std::uint8_t {
    Session, ///< Session header.
    Frame,   ///< CAN frame.
    Gap,     ///< Synthetic loss.
    Status,  ///< Channel status.
};

/// @brief EDS binding metadata stored in the session header.
struct SessionEdsBinding {
    std::uint16_t channel_id{0}; ///< Channel identity.
    std::uint8_t node_id{0};     ///< CANopen node id.
    std::string alias{};         ///< Display alias.
    std::string path{};          ///< EDS path.
    std::uint64_t hash{0};       ///< EDS content hash.
    std::uint32_t colour{0};     ///< RGBA colour.
};

/// @brief Write one EDS binding metadata object as JSON.
void to_json(nlohmann::json& j, const SessionEdsBinding& binding);
/// @brief Read one EDS binding metadata object without throwing.
void from_json(const nlohmann::json& j, SessionEdsBinding& binding) noexcept;

/// @brief First JSONL line.
struct CanViewSessionHeader {
    static constexpr std::string_view format_name{"nexigen.canview.session"}; ///< Canonical format.
    static constexpr std::uint32_t format_version{1U}; ///< Canonical version.

    std::string format{std::string(format_name)}; ///< Format tag.
    std::uint32_t version{format_version};        ///< Version.
    std::int64_t created_ns{0};                   ///< Creation time.
    std::uint64_t boot_token{0};                  ///< Boot identity.
    std::string timebase{};                       ///< Time-base segment or mode.
    std::vector<CanViewChannel> channels{};       ///< Channel profiles.
    std::vector<SessionEdsBinding> eds{};         ///< EDS bindings.
    std::uint64_t frame_count{0};                 ///< Exported frames.
    std::uint64_t gap_count{0};                   ///< Exported gaps.
};

/// @brief One frame line.
struct SessionFrameRecord {
    std::uint64_t seq{0};                     ///< Local identity at export time.
    std::uint64_t hw_ts_ns{0};                ///< Hardware timestamp.
    std::optional<std::int64_t> utc_est_ns{}; ///< Estimated UTC, optional.
    std::string utc_source{};                 ///< Anchor source when UTC is present.
    std::int64_t delta_ns{0};                 ///< Delta to the previous visible record.
    std::int64_t delta_id_ns{0};              ///< Delta to the previous frame with the same key.
    std::string channel_name{};               ///< Channel display name.
    nexigen::hw::can::CanMessage message{};   ///< Frame payload.
    std::string co_decoded{};                 ///< Exported CANopen text, authoritative offline.
};

/// @brief One synthetic loss line.
struct SessionGapRecord {
    std::uint64_t seq{0};                            ///< Local identity at export time.
    CanRecordKind kind{CanRecordKind::TransportGap}; ///< Loss kind.
    GapReason reason{GapReason::TransportGap};       ///< Loss reason.
    std::uint64_t lost{0};                           ///< Missing count.
    bool count_exact{false};                         ///< Whether the count is exact.
    std::uint64_t hw_ts_ns{0};                       ///< Reference hardware time.
    std::uint16_t channel_id{0};                     ///< Channel identity.
    std::string channel_name{};                      ///< Channel display name.
    std::uint16_t time_epoch{0};                     ///< UTC anchor epoch.
};

/// @brief One status line.
struct SessionStatusRecord {
    std::uint64_t seq{0};                        ///< Local identity at export time.
    std::uint16_t channel_id{0};                 ///< Channel identity.
    std::string channel_name{};                  ///< Channel display name.
    nexigen::hw::can::CanChannelStatus status{}; ///< Driver snapshot.
};

/// @brief One imported record.
struct SessionRecord {
    CanViewRecordType type{CanViewRecordType::Frame}; ///< Record kind.
    SessionFrameRecord frame{};                       ///< Frame payload.
    SessionGapRecord gap{};                           ///< Gap payload.
    SessionStatusRecord status{};                     ///< Status payload.
};

/// @brief Encode a payload as lowercase hex.
/// @param message Source message.
/// @return Hex string of `dlc` bytes.
[[nodiscard]] std::string payload_to_hex(const nexigen::hw::can::CanMessage& message);

/// @brief Encode a payload for the message table.
///
/// Bytes are separated by one space, with an extra space after every fourth
/// byte so groups of four stay readable.
///
/// @param message Source message.
/// @return Grouped lowercase hex string of `dlc` bytes.
[[nodiscard]] std::string payload_to_grouped_hex(const nexigen::hw::can::CanMessage& message);

/// @brief Decode a hex payload into a message.
/// @param text Hex text, optionally separated by spaces.
/// @param message Destination; payload bytes and `dlc` are updated.
/// @return `true` when at least one byte parsed.
bool payload_from_hex(std::string_view text, nexigen::hw::can::CanMessage& message) noexcept;

/// @brief Write a header line.
void to_json(nlohmann::json& j, const CanViewSessionHeader& header);
/// @brief Read a header line without throwing.
void from_json(const nlohmann::json& j, CanViewSessionHeader& header) noexcept;
/// @brief Write a frame line.
void to_json(nlohmann::json& j, const SessionFrameRecord& record);
/// @brief Read a frame line without throwing.
void from_json(const nlohmann::json& j, SessionFrameRecord& record) noexcept;
/// @brief Write a gap line.
void to_json(nlohmann::json& j, const SessionGapRecord& record);
/// @brief Read a gap line without throwing.
void from_json(const nlohmann::json& j, SessionGapRecord& record) noexcept;
/// @brief Write a status line.
void to_json(nlohmann::json& j, const SessionStatusRecord& record);
/// @brief Read a status line without throwing.
void from_json(const nlohmann::json& j, SessionStatusRecord& record) noexcept;

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `CanViewRecordType`.
template <> struct EnumTraits<nexigen::canview::CanViewRecordType> {
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::CanViewRecordType value; ///< Enumerator.
        std::string_view name;                     ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::canview::CanViewRecordType::Session, "session"},
        {nexigen::canview::CanViewRecordType::Frame, "frame"},
        {nexigen::canview::CanViewRecordType::Gap, "gap"},
        {nexigen::canview::CanViewRecordType::Status, "status"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical lowercase record-type name.
[[nodiscard]] constexpr std::string_view to_string(CanViewRecordType value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse a record type.
[[nodiscard]] constexpr bool from_string(std::string_view name, CanViewRecordType& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write a record type as JSON.
inline void to_json(nlohmann::json& j, const CanViewRecordType value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read a record type without throwing.
inline void from_json(const nlohmann::json& j, CanViewRecordType& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::canview
