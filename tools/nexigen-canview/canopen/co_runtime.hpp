// nexigen-canview — per-node PDO mapping snapshots and remap epochs.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "eds_dictionary.hpp"

namespace nexigen::canview {

class CobIdRouter;

/// @brief Immutable PDO mapping and COB-ID state of one epoch.
struct PdoSnapshot {
    std::uint8_t epoch{0};                   ///< Mapping epoch.
    bool valid{false};                       ///< Whether this snapshot is populated.
    std::array<EdsPdoMapping, 4> tpdo{};     ///< TPDO1..4 mappings.
    std::array<EdsPdoMapping, 4> rpdo{};     ///< RPDO1..4 mappings.
    std::array<std::uint32_t, 4> tpdo_cob{}; ///< TPDO1..4 COB-IDs; zero = disabled.
    std::array<std::uint32_t, 4> rpdo_cob{}; ///< RPDO1..4 COB-IDs.
};

/// @brief Mutable decode state for one bound node.
class CoNodeRuntime {
public:
    /// @brief Number of retained mapping epochs.
    static constexpr std::size_t snapshot_count{16U};

    /// @brief Initialise from the binding dictionary.
    /// @param dictionary Bound dictionary.
    /// @param channel_id Channel identity.
    /// @param node_id Node id.
    /// @param slot Registry slot.
    void init(const EdsDictionary& dictionary, std::uint16_t channel_id, std::uint8_t node_id,
              std::uint16_t slot) noexcept;
    /// @brief Drop every snapshot.
    void reset() noexcept;

    /// @brief Whether the runtime was initialised.
    /// @return `true` after `init()`.
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    /// @brief Dictionary content hash.
    /// @return Hash of the dictionary used to build the runtime.
    [[nodiscard]] std::uint64_t dictionary_hash() const noexcept { return dictionary_hash_; }
    /// @brief Current epoch.
    /// @return Mapping epoch.
    [[nodiscard]] std::uint8_t current_epoch() const noexcept;
    /// @brief Latest snapshot.
    /// @return Snapshot reference.
    [[nodiscard]] const PdoSnapshot& current() const noexcept;

    /// @brief Find one epoch.
    /// @param epoch Mapping epoch.
    /// @param stale Receives `true` when the epoch was evicted and the current
    ///        snapshot is substituted.
    /// @return Snapshot reference, or `nullptr` before `init()`.
    [[nodiscard]] const PdoSnapshot* snapshot(std::uint8_t epoch, bool& stale) const noexcept;

    /// @brief Apply one successful SDO write.
    /// @param index Object index.
    /// @param subindex Object subindex.
    /// @param data Little-endian payload.
    /// @param size Payload bytes.
    /// @param router Router receiving COB-ID overrides, or `nullptr`.
    /// @return `true` when the write changed the mapping/COB-ID state.
    bool apply_write(std::uint16_t index, std::uint8_t subindex, const std::uint8_t* data,
                     std::size_t size, CobIdRouter* router);

    /// @brief Number of retained snapshots.
    /// @return Count.
    [[nodiscard]] std::size_t retained_epochs() const noexcept { return count_; }

private:
    /// @brief Append one prepared snapshot.
    void allocate(PdoSnapshot snapshot) noexcept;
    /// @brief Build snapshot zero from the dictionary defaults.
    void build_defaults() noexcept;
    /// @brief Default COB-ID for one PDO.
    [[nodiscard]] static std::uint32_t default_cob(bool transmit, std::uint8_t pdo,
                                                   std::uint8_t node_id) noexcept;
    /// @brief Append a mapping entry to one snapshot.
    static void set_mapping(PdoSnapshot& snapshot, bool transmit, std::uint8_t pdo,
                            std::uint8_t subindex, std::uint32_t value) noexcept;
    /// @brief Apply a mapping-object write.
    bool apply_mapping_write(std::uint16_t index, std::uint8_t subindex,
                             std::uint32_t value) noexcept;
    /// @brief Apply a COB-ID communication-object write.
    bool apply_cob_write(std::uint16_t index, std::uint8_t subindex, std::uint32_t value,
                         CobIdRouter* router);
    /// @brief Parse a COB-ID value.
    [[nodiscard]] static bool parse_cob_id(std::uint32_t value, std::uint32_t& resolved,
                                           bool& disabled) noexcept;
    /// @brief Update the router after a COB-ID change.
    void update_router(CobIdRouter* router, bool transmit, std::uint8_t pdo, std::uint32_t old_cob,
                       std::uint32_t new_cob, bool disabled) noexcept;

    std::array<PdoSnapshot, snapshot_count> snapshots_{}; ///< Epoch ring.
    std::size_t head_{0};                                 ///< Oldest snapshot.
    std::size_t count_{0};                                ///< Retained snapshots.
    const EdsDictionary* dictionary_{nullptr};            ///< Borrowed dictionary.
    std::uint16_t channel_id_{0};                         ///< Channel identity.
    std::uint8_t node_id_{0};                             ///< Node id.
    std::uint16_t slot_{0};                               ///< Registry slot.
    std::uint64_t dictionary_hash_{0};                    ///< Dictionary hash.
    bool initialized_{false};                             ///< Whether a snapshot exists.
};

} // namespace nexigen::canview
