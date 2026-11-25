// nexigen-canview — fixed-capacity, absolute-indexed record history.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_message.hpp>

#include "can_record_view.hpp"

namespace nexigen::canview {

/// @brief Ring of CAN records with stable absolute numbers and eviction.
///
/// One anonymous `mmap` holds six cache-line-aligned parallel arrays totalling
/// 126 bytes per slot. The UI thread is the only writer and reader of the
/// history, so the indices are plain `std::uint64_t` and there are no atomics
/// or memory orders in this class. The mapping never grows after creation.
class CanRecordRing {
public:
    /// @brief Allocator seam used by tests to exercise the failure path.
    using MapFunction = void* (*)(std::size_t);

    /// @brief Bytes stored per slot across the six parallel arrays.
    static constexpr std::size_t bytes_per_slot{126U};

    /// @brief Default allocator: anonymous, non-reserving `mmap`.
    /// @param bytes Byte count to map.
    /// @return Mapping address or `MAP_FAILED`.
    [[nodiscard]] static void* default_map(std::size_t bytes) noexcept;

    /// @brief Create a ring with an explicit power-of-two capacity.
    /// @param capacity Slot count; must be a positive power of two.
    /// @param map_function Allocator seam; defaults to `default_map`.
    /// @return The ring, or `InvalidArgument`/`SystemError`.
    [[nodiscard]] static nexigen::core::Result<CanRecordRing>
    create(std::size_t capacity, MapFunction map_function = &CanRecordRing::default_map);

    /// @brief Create a ring sized from a hard memory budget.
    /// @param max_memory_mb Total model budget in MiB.
    /// @param map_function Allocator seam.
    /// @return The ring, or the same failures as `create`.
    [[nodiscard]] static nexigen::core::Result<CanRecordRing>
    create_for_budget(std::uint64_t max_memory_mb,
                      MapFunction map_function = &CanRecordRing::default_map);

    /// @brief Move-construct a ring, transferring the mapping.
    CanRecordRing(CanRecordRing&& other) noexcept;
    /// @brief Move-assign a ring, releasing the previous mapping.
    CanRecordRing& operator=(CanRecordRing&& other) noexcept;
    /// @brief Destroy the ring and unmap its storage.
    ~CanRecordRing();

    /// @brief Copying is disabled: the mapping has one owner.
    CanRecordRing(const CanRecordRing&) = delete;
    /// @brief Copy assignment is disabled.
    CanRecordRing& operator=(const CanRecordRing&) = delete;

    /// @brief Append one record.
    /// @param message Frame or synthetic marker payload.
    /// @param delta_same_id_ns Per-key delta for frames, gap count for losses.
    /// @param co_node_index CANopen registry slot, or `CanRecordView::no_node`.
    /// @param meta Packed `record_meta`.
    /// @param time_epoch UTC anchor epoch at ingest.
    /// @param co_map_epoch CANopen mapping snapshot.
    /// @return The assigned absolute record number.
    std::uint64_t push(const nexigen::hw::can::CanMessage& message, std::int64_t delta_same_id_ns,
                       std::uint16_t co_node_index, std::uint8_t meta, std::uint16_t time_epoch,
                       std::uint8_t co_map_epoch) noexcept;

    /// @brief Overwrite the CANopen node index of one resident record.
    /// @param seq Absolute record number.
    /// @param co_node_index Registry slot, or `CanRecordView::no_node`.
    /// @return `true` when the record was resident and updated.
    bool set_co_node_index(std::uint64_t seq, std::uint16_t co_node_index) noexcept;

    /// @brief Read one record by absolute number.
    /// @param seq Absolute record number.
    /// @return The view when the record is resident.
    [[nodiscard]] std::optional<CanRecordView> find(std::uint64_t seq) const noexcept;

    /// @brief Oldest retained absolute number.
    /// @return `head`.
    [[nodiscard]] std::uint64_t head() const noexcept { return head_; }
    /// @brief One past the newest absolute number.
    /// @return `tail`.
    [[nodiscard]] std::uint64_t tail() const noexcept { return tail_; }
    /// @brief Number of retained records.
    /// @return `tail - head`.
    [[nodiscard]] std::uint64_t size() const noexcept { return tail_ - head_; }
    /// @brief Capacity in slots.
    /// @return `capacity_`.
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    /// @brief Total evictions since creation.
    /// @return Eviction counter.
    [[nodiscard]] std::uint64_t evictions() const noexcept { return evictions_; }
    /// @brief Mapping byte count.
    /// @return Bytes reserved by the single allocation.
    [[nodiscard]] std::size_t mapped_bytes() const noexcept { return mapped_bytes_; }

    /// @brief Drop every record and reset the counters.
    void clear() noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with counters and capacities.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Partition offsets inside the single mapping.
    struct Layout {
        std::size_t messages{0};   ///< `CanMessage` array offset.
        std::size_t deltas{0};     ///< Per-key delta array offset.
        std::size_t nodes{0};      ///< CANopen node-index array offset.
        std::size_t metas{0};      ///< Packed metadata array offset.
        std::size_t epochs{0};     ///< UTC epoch array offset.
        std::size_t map_epochs{0}; ///< CANopen mapping epoch array offset.
        std::size_t total{0};      ///< Total mapping size.
    };

    /// @brief Compute a layout for a capacity.
    [[nodiscard]] static Layout layout_for(std::size_t capacity) noexcept;

    /// @brief Construct an empty, uninitialised ring.
    CanRecordRing() noexcept = default;

    /// @brief Evict the oldest resident record.
    void evict_one() noexcept;
    /// @brief Whether a record number is currently resident.
    [[nodiscard]] bool resident(std::uint64_t seq) const noexcept {
        return seq >= head_ && seq < tail_;
    }

    void* mapping_{nullptr};                          ///< Single mmap base.
    std::size_t mapped_bytes_{0};                     ///< Mapping length.
    std::size_t capacity_{0};                         ///< Slot count.
    std::uint64_t head_{0};                           ///< Oldest retained number.
    std::uint64_t tail_{0};                           ///< One past the newest number.
    std::uint64_t evictions_{0};                      ///< Eviction counter.
    nexigen::hw::can::CanMessage* messages_{nullptr}; ///< Frame storage.
    std::int64_t* deltas_{nullptr};                   ///< Per-key delta or gap count.
    std::uint16_t* nodes_{nullptr};                   ///< CANopen node index.
    std::uint8_t* metas_{nullptr};                    ///< Packed metadata.
    std::uint16_t* epochs_{nullptr};                  ///< UTC anchor epoch.
    std::uint8_t* map_epochs_{nullptr};               ///< CANopen mapping epoch.
};

} // namespace nexigen::canview
