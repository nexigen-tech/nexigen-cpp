// nexigen-canview — CANopen node bindings keyed by channel and node.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>

#include "canopen_types.hpp"
#include "eds_dictionary.hpp"

namespace nexigen::canview {

/// @brief One `(channel_id, node_id)` EDS binding.
struct NodeBinding {
    std::uint16_t registry_slot{0};                       ///< Owning registry slot.
    std::uint16_t channel_id{0};                          ///< Channel identity.
    std::uint8_t node_id{0};                              ///< CANopen node id (1..127).
    std::string alias{};                                  ///< Display alias.
    std::uint32_t colour{0xFF4FC3F7U};                    ///< RGBA display colour.
    std::shared_ptr<const EdsDictionary> dictionary{};    ///< Shared dictionary.
    std::string eds_path{};                               ///< Source path.
    std::uint64_t eds_hash{0};                            ///< Dictionary content hash.
    NmtState nmt_state{NmtState::Unknown};                ///< Last observed NMT state.
    std::uint64_t heartbeat_timeout_ns{3'000'000'000ULL}; ///< Stale after this age.
    std::uint64_t last_heartbeat_ns{0};                   ///< Last heartbeat time.
    bool heartbeat_seen{false};                           ///< Whether any heartbeat arrived.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the identity and EDS hash.
    [[nodiscard]] std::string to_string() const;

    /// @brief Whether the heartbeat is older than the timeout.
    /// @param now_ns Current host monotonic time.
    /// @return `true` when a heartbeat was seen and expired.
    [[nodiscard]] bool stale(std::uint64_t now_ns) const noexcept;
};

/// @brief Write a binding as JSON (without the parsed dictionary).
void to_json(nlohmann::json& j, const NodeBinding& binding);
/// @brief Read a binding from JSON without throwing (alias-only).
void from_json(const nlohmann::json& j, NodeBinding& binding) noexcept;

/// @brief Bounded registry of EDS bindings.
class CanOpenRegistry {
public:
    /// @brief Maximum bindings.
    static constexpr std::size_t capacity{128U};

    /// @brief Bind one `(channel, node)` pair.
    /// @param channel_id Channel identity.
    /// @param node_id Node id (1..127).
    /// @param alias Display alias.
    /// @param colour RGBA colour.
    /// @param dictionary Shared dictionary, or `nullptr` for an alias-only binding.
    /// @param eds_path Source path.
    /// @param replace Replace an existing pair instead of rejecting it.
    /// @return Registry slot, or `InvalidArgument`/`Conflict`/`ResourceExhausted`.
    [[nodiscard]] nexigen::core::Result<std::uint16_t>
    bind(std::uint16_t channel_id, std::uint8_t node_id, std::string alias, std::uint32_t colour,
         std::shared_ptr<const EdsDictionary> dictionary, std::string eds_path,
         bool replace = false);

    /// @brief Remove one binding.
    /// @param channel_id Channel identity.
    /// @param node_id Node id.
    /// @return `true` when removed.
    bool unbind(std::uint16_t channel_id, std::uint8_t node_id) noexcept;
    /// @brief Remove one slot.
    /// @param slot Registry slot.
    /// @return `true` when removed.
    bool unbind_slot(std::uint16_t slot) noexcept;

    /// @brief Look up a binding pair.
    /// @return Binding or `nullptr`.
    [[nodiscard]] const NodeBinding* find(std::uint16_t channel_id,
                                          std::uint8_t node_id) const noexcept;
    /// @brief Look up a binding pair.
    /// @return Binding or `nullptr`.
    [[nodiscard]] NodeBinding* find(std::uint16_t channel_id, std::uint8_t node_id) noexcept;
    /// @brief Look up one slot.
    /// @return Binding or `nullptr`.
    [[nodiscard]] const NodeBinding* at(std::uint16_t slot) const noexcept;
    /// @brief Look up one slot.
    /// @return Binding or `nullptr`.
    [[nodiscard]] NodeBinding* at(std::uint16_t slot) noexcept;

    /// @brief Number of bindings.
    /// @return Count.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    /// @brief Monotonic change counter.
    /// @return Revision incremented on every bind/unbind/state change.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    /// @brief Refresh bindings that use one EDS path.
    /// @param path EDS path.
    /// @param dictionary New dictionary (may be `nullptr`).
    /// @param hash New content hash.
    /// @return Number of bindings updated.
    std::size_t refresh_dictionary(std::string_view path,
                                   std::shared_ptr<const EdsDictionary> dictionary,
                                   std::uint64_t hash) noexcept;

    /// @brief Record one heartbeat/boot-up state sample.
    /// @param slot Registry slot.
    /// @param state Observed NMT state.
    /// @param now_ns Host monotonic time.
    void observe_heartbeat(std::uint16_t slot, NmtState state, std::uint64_t now_ns) noexcept;
    /// @brief Set the NMT state without refreshing the heartbeat age.
    /// @param slot Registry slot.
    /// @param state New state.
    void set_nmt_state(std::uint16_t slot, NmtState state) noexcept;

    /// @brief Drop every binding.
    void clear() noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with binding count and revision.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Find the slot for a pair.
    /// @return Registry slot or `capacity`.
    [[nodiscard]] std::uint16_t slot_for(std::uint16_t channel_id,
                                         std::uint8_t node_id) const noexcept;

    std::array<std::optional<NodeBinding>, capacity> slots_{}; ///< Binding storage.
    std::size_t size_{0};                                      ///< Binding count.
    std::uint64_t revision_{0};                                ///< Change counter.
};

} // namespace nexigen::canview
