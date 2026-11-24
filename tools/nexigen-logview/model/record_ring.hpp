// nexigen-logview — fixed-capacity, absolute-indexed record history.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/log_frame.hpp>

#include "app_registry.hpp"
#include "record_view.hpp"

namespace nexigen::logview {

/// @brief Ring of records with stable absolute numbers and overwrite-oldest eviction.
///
/// One anonymous `mmap` holds every large array; the ring never reallocates.
/// The UI thread is the only writer and reader of the history, so the indices
/// are plain `std::uint64_t` and there are no atomics or memory orders in this
/// class. Side arrays keep selection and bookmarks slot-indexed, and both are
/// cleared when a slot is reused.
class RecordRing {
public:
    /// @brief Allocator seam used by tests to exercise the failure path.
    using MapFunction = void* (*)(std::size_t);

    /// @brief Default allocator: anonymous, non-reserving `mmap`.
    /// @param bytes Byte count to map.
    /// @return Mapping address or `MAP_FAILED`.
    [[nodiscard]] static void* default_map(std::size_t bytes) noexcept;

    /// @brief Create a ring with an explicit power-of-two capacity.
    /// @param capacity Slot count; must be a positive power of two.
    /// @param map_function Allocator seam; defaults to `default_map`.
    /// @return The ring, or `InvalidArgument`/`SystemError`.
    [[nodiscard]] static nexigen::core::Result<RecordRing>
    create(std::size_t capacity, MapFunction map_function = &RecordRing::default_map);

    /// @brief Create a ring sized from a hard memory budget.
    /// @param budget_bytes Total model budget in bytes.
    /// @param map_function Allocator seam.
    /// @return The ring, or the same failures as `create`.
    [[nodiscard]] static nexigen::core::Result<RecordRing>
    create_for_budget(std::uint64_t budget_bytes,
                      MapFunction map_function = &RecordRing::default_map);

    /// @brief Derive the power-of-two history capacity for a budget.
    /// @param budget_bytes Total model budget in bytes.
    /// @return Capacity, at least 1024.
    [[nodiscard]] static std::size_t capacity_for_budget(std::uint64_t budget_bytes) noexcept;

    /// @brief Move-construct a ring, transferring the mapping.
    RecordRing(RecordRing&& other) noexcept;
    /// @brief Move-assign a ring, releasing the previous mapping.
    RecordRing& operator=(RecordRing&& other) noexcept;
    /// @brief Destroy the ring and unmap its storage.
    ~RecordRing();

    /// @brief Copying is disabled: the mapping has one owner.
    RecordRing(const RecordRing&) = delete;
    /// @brief Copy assignment is disabled.
    RecordRing& operator=(const RecordRing&) = delete;

    /// @brief Append one received frame.
    /// @param frame Frame to store.
    /// @return The assigned absolute record number.
    std::uint64_t push_frame(nexigen::logging::LogFrame frame) noexcept;

    /// @brief Append one synthetic loss record.
    /// @param kind Synthetic kind; a frame kind is rejected.
    /// @param count Number of missing records.
    /// @param timestamp_ns Reference timestamp for display.
    /// @return The assigned absolute record number.
    std::uint64_t push_gap(RecordKind kind, std::uint64_t count,
                           std::uint64_t timestamp_ns) noexcept;

    /// @brief Read one record by absolute number.
    /// @param seq Absolute record number.
    /// @return The view when the record is resident.
    [[nodiscard]] std::optional<RecordView> find(std::uint64_t seq) const noexcept;

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

    /// @brief Application registry.
    /// @return Mutable reference.
    [[nodiscard]] AppRegistry& apps() noexcept { return apps_; }
    /// @brief Application registry.
    /// @return Const reference.
    [[nodiscard]] const AppRegistry& apps() const noexcept { return apps_; }
    /// @brief Drop every record and reset session counters.
    void clear() noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with counters and capacities.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Partition offsets inside the single mapping.
    struct Layout {
        std::size_t slots{0}; ///< Frame array offset.
        std::size_t kinds{0}; ///< Kind array offset.
        std::size_t prev{0};  ///< Per-application delta array offset.
        std::size_t total{0}; ///< Total mapping size.
    };

    /// @brief Compute a layout for a capacity.
    [[nodiscard]] static Layout layout_for(std::size_t capacity) noexcept;

    /// @brief Construct an empty, uninitialised ring.
    RecordRing() noexcept = default;

    /// @brief Evict the oldest resident record.
    void evict_one() noexcept;
    /// @brief Whether a record number is currently resident.
    [[nodiscard]] bool resident(std::uint64_t seq) const noexcept {
        return seq >= head_ && seq < tail_;
    }

    void* mapping_{nullptr};                     ///< Single mmap base.
    std::size_t mapped_bytes_{0};                ///< Mapping length.
    std::size_t capacity_{0};                    ///< Slot count.
    std::uint64_t head_{0};                      ///< Oldest retained number.
    std::uint64_t tail_{0};                      ///< One past the newest number.
    std::uint64_t evictions_{0};                 ///< Eviction counter.
    nexigen::logging::LogFrame* slots_{nullptr}; ///< Frame storage.
    std::uint8_t* kinds_{nullptr};               ///< Kind storage.
    std::int64_t* prev_{nullptr};                ///< Per-application delta or gap count.
    AppRegistry apps_{};                         ///< Application table.
};

/// @brief Free diagnostic mirror of `RecordRing::to_string`.
/// @param ring Ring to render.
/// @return The same text.
[[nodiscard]] std::string to_string(const RecordRing& ring);

} // namespace nexigen::logview
