// nexigen-canview — view scope (change add-can-monitor-view).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canview {

/// @brief Scope a view is bound to.
enum class ViewKind : std::uint8_t {
    All,     ///< Every configured channel; the channel mask is editable.
    Channel, ///< One `channel_id`.
    Node,    ///< One `(channel_id, node_id)` CANopen binding.
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `ViewKind`.
template <> struct EnumTraits<nexigen::canview::ViewKind> {
    /// @brief One value/name row.
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::ViewKind value; ///< Enumerator. ///< Enumerator.
        std::string_view name;            ///< Canonical name.
    };
    /// @brief Canonical table.
    /// @brief Canonical table.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::canview::ViewKind::All, "All"},
        {nexigen::canview::ViewKind::Channel, "Channel"},
        {nexigen::canview::ViewKind::Node, "Node"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical name of a view kind.
/// @param kind Kind to render.
/// @return Canonical name or `Unknown`.
[[nodiscard]] constexpr std::string_view to_string(ViewKind kind) noexcept {
    return core::enum_name(kind);
}

/// @brief Parse a canonical view-kind name.
/// @param name Candidate name.
/// @param out Destination, unchanged on a miss.
/// @return `true` when a name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, ViewKind& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write a view kind as JSON.
/// @param j Destination.
/// @param kind Kind to serialize.
inline void to_json(nlohmann::json& j, const ViewKind kind) {
    core::mandate::enum_to_json(j, kind);
}

/// @brief Read a view kind from JSON without throwing.
/// @param j Source.
/// @param kind Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, ViewKind& kind) noexcept {
    core::mandate::enum_from_json(j, kind);
}

} // namespace nexigen::canview
