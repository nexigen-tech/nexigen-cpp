// nexigen-canview — per-view record filter (change add-can-monitor-view).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/hw/can/can_message_origin.hpp>

#include "can_record_view.hpp"
#include "record_source.hpp"

namespace nexigen::canview {

/// @brief Optional aliases used by the textual filter.
struct FilterLabels {
    std::string_view channel_alias{}; ///< Channel display name.
    std::string_view node_alias{};    ///< CANopen node alias, empty when unbound.
};

/// @brief One view's filter criteria.
///
/// Trace rebuilds scan records in order, so `matches()` never allocates,
/// formats, or takes a lock. The text query is parsed once by `set_text()`.
struct FilterCriteria {
    /// @brief Bit for one `channel_id` in the 32-bit channel mask.
    /// @param channel_id Channel identity.
    /// @return Mask bit.
    [[nodiscard]] static constexpr std::uint32_t channel_bit(std::uint16_t channel_id) noexcept {
        return 1U << (channel_id & 31U);
    }

    /// @brief Length of the widest textual payload.
    static constexpr std::size_t max_query_bytes{64U};

    std::uint32_t channel_mask{0xFFFFFFFFU};                ///< Visible channels.
    std::uint32_t id{0};                                    ///< Identifier to match.
    std::uint32_t id_mask{0};                               ///< Identifier mask.
    bool id_filter_active{false};                           ///< Whether the ID filter applies.
    std::array<std::uint64_t, 2> node_mask{{~0ULL, ~0ULL}}; ///< 128 binding slots.
    bool node_filter_active{false};                         ///< Whether the node filter applies.
    std::uint8_t kind_mask{0x1FU};                          ///< Visible `CanRecordKind` values.
    std::uint8_t origin_mask{0x07U};                        ///< Visible `CanMessageOrigin` values.
    bool errors_only{false};                                ///< Error/overload/invalid + gaps only.
    bool show_bus_idle{false}; ///< Bus-idle heartbeats are hidden by default.
    bool show_gaps{true};      ///< Whether synthetic losses are visible.
    std::string text{};        ///< Raw query, parsed by `set_text()`.

    /// @brief Set the textual query and refresh the hex cache.
    /// @param value New query; an empty string disables textual matching.
    void set_text(std::string value);

    /// @brief Whether one channel bit is visible.
    /// @param channel_id Channel identity.
    /// @return `true` when the mask includes the channel.
    [[nodiscard]] bool channel_visible(std::uint16_t channel_id) const noexcept {
        return (channel_mask & channel_bit(channel_id)) != 0U;
    }

    /// @brief Whether one CANopen registry slot is visible.
    /// @param slot Registry slot, or `CanRecordView::no_node`.
    /// @return `true` when the node mask includes the slot.
    [[nodiscard]] bool node_visible(std::uint16_t slot) const noexcept {
        if (slot >= 128U) {
            return false;
        }
        return (node_mask[slot / 64U] & (1ULL << (slot % 64U))) != 0U;
    }

    /// @brief Set one CANopen slot bit.
    /// @param slot Registry slot.
    /// @param visible New state.
    void set_node_visible(std::uint16_t slot, bool visible) noexcept;

    /// @brief Whether a record passes this filter.
    /// @param record Record to test.
    /// @param labels Channel/node aliases for the textual filter.
    /// @return `true` when visible.
    [[nodiscard]] bool matches(const CanRecordView& record,
                               const FilterLabels& labels = {}) const noexcept;

    /// @brief Resolve a record and apply this filter.
    /// @param source History to read.
    /// @param seq Absolute record number.
    /// @param labels Channel/node aliases.
    /// @return `true` when resident and visible.
    [[nodiscard]] bool matches(const RecordSource& source, std::uint64_t seq,
                               const FilterLabels& labels = {}) const noexcept;

    /// @brief Case-insensitive textual match used by filters and find.
    /// @param record Record to test.
    /// @param labels Channel/node aliases.
    /// @return `true` when the query is empty or matches.
    [[nodiscard]] bool text_matches(const CanRecordView& record,
                                    const FilterLabels& labels) const noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the active criteria.
    [[nodiscard]] std::string to_string() const;

private:
    std::array<std::uint8_t, max_query_bytes> hex_{}; ///< Parsed hex pattern.
    std::uint8_t hex_length_{0};                      ///< Valid pattern bytes.
    bool hex_mode_{false};                            ///< Query is a byte sequence.

    /// @brief Whether a frame is error-like for `errors_only`.
    [[nodiscard]] static bool is_error_like(const CanRecordView& record) noexcept;
    /// @brief Loss-record branch of `matches`.
    [[nodiscard]] bool matches_loss(const CanRecordView& record,
                                    const FilterLabels& labels) const noexcept;
    /// @brief Bus-idle branch of `matches`.
    [[nodiscard]] bool matches_bus_idle(const CanRecordView& record,
                                        const FilterLabels& labels) const noexcept;
    /// @brief Frame branch of `matches`.
    [[nodiscard]] bool matches_frame(const CanRecordView& record,
                                     const FilterLabels& labels) const noexcept;
};

/// @brief Write filter criteria as JSON.
/// @param j Destination.
/// @param criteria Criteria to serialize.
void to_json(nlohmann::json& j, const FilterCriteria& criteria);

/// @brief Read filter criteria from JSON without throwing.
/// @param j Source.
/// @param criteria Destination.
void from_json(const nlohmann::json& j, FilterCriteria& criteria) noexcept;

} // namespace nexigen::canview
