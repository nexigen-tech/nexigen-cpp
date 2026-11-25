// nexigen-canview — keyed Overwrite table (change add-can-monitor-view).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <nexigen/hw/can/can_message.hpp>

#include "can_record_view.hpp"
#include "overwrite_key.hpp"

namespace nexigen::canview {

/// @brief Period and rate statistics of one Overwrite row.
struct OverwriteStats {
    std::uint64_t count{0};     ///< Lifetime frame count.
    std::uint64_t msg_per_s{0}; ///< Sliding one-second rate.
    std::int64_t delta_cur{0};  ///< Latest delta, including negative values.
    std::int64_t delta_min{0};  ///< Minimum valid period.
    std::int64_t delta_max{0};  ///< Maximum valid period.
    std::int64_t delta_avg{0};  ///< Average valid period.
    std::int64_t jitter{0};     ///< Peak-to-peak period variation.
    bool has_delta{false};      ///< Whether a valid period exists.
    bool anomalous{false};      ///< Latest delta exceeded the threshold.
    bool error_row{false};      ///< Error/overload row; period is shown as `-`.
};

/// @brief One `(channel, id, extended, kind)` row.
struct OverwriteRow {
    static constexpr std::size_t rate_buckets{10U}; ///< 100 ms buckets covering 1 s.
    static constexpr std::size_t delta_window{32U}; ///< Valid periods kept.

    OverwriteKey key{};                    ///< Row identity.
    nexigen::hw::can::CanMessage latest{}; ///< Latest payload.
    std::uint64_t last_seq{0};             ///< Latest record number.
    std::uint64_t count{0};                ///< Lifetime count.
    std::uint64_t first_ns{0};             ///< First hardware timestamp.
    std::uint64_t last_ns{0};              ///< Latest hardware timestamp.
    std::uint64_t last_touch{0};           ///< LRU stamp.

    std::array<std::uint64_t, rate_buckets> rate_count{}; ///< Per-bucket counts.
    std::array<std::int64_t, rate_buckets> rate_epoch{};  ///< Bucket indices.

    std::array<std::int64_t, delta_window> deltas{}; ///< Valid period window.
    std::size_t delta_head{0};                       ///< Oldest period.
    std::size_t delta_count{0};                      ///< Valid periods.
    std::int64_t delta_cur{0};                       ///< Latest delta.
    bool has_previous{false};                        ///< Whether a previous frame exists.
    bool skip_next_delta{false};                     ///< The next period follows a gap.
    bool anomalous{false};                           ///< Latest delta exceeded the threshold.
    bool error_row{false};                           ///< Error/overload row.

    std::array<bool, 64> byte_changed{};          ///< Payload bytes that changed.
    std::array<std::uint64_t, 64> byte_at{};      ///< Monotonic UI time of the change.
    std::array<std::uint8_t, 64> previous_data{}; ///< Previous payload.
    bool has_payload{false};                      ///< Whether a payload was observed.
    std::uint8_t previous_dlc{0};                 ///< Previous payload length.
    std::uint64_t previous_timestamp{0};          ///< Previous hardware time.

    /// @brief Fold one frame into the row.
    /// @param message New payload.
    /// @param now_ns UI monotonic time.
    /// @param anomaly_threshold_ns Period outlier threshold.
    void observe(const nexigen::hw::can::CanMessage& message, std::uint64_t now_ns,
                 std::int64_t anomaly_threshold_ns) noexcept;

    /// @brief Clear the period window and mark the next period anomalous.
    void reset_period() noexcept;

    /// @brief Whether one byte changed recently.
    /// @param index Byte index.
    /// @param now_ns UI monotonic time.
    /// @param fade_ns Fade duration.
    /// @return `true` while the highlight is active.
    [[nodiscard]] bool byte_fading(std::size_t index, std::uint64_t now_ns,
                                   std::uint64_t fade_ns) const noexcept;

    /// @brief Current statistics.
    /// @param now_ns UI monotonic time for rate expiry.
    /// @return Stats snapshot.
    [[nodiscard]] OverwriteStats stats(std::uint64_t now_ns) const noexcept;
};

/// @brief Bounded open-addressing table with LRU eviction.
class OverwriteTable {
public:
    /// @brief Default row capacity.
    static constexpr std::size_t default_capacity{4096U};

    /// @brief Allocate the default table.
    OverwriteTable() { init(default_capacity); }

    /// @brief Allocate the table.
    /// @param capacity Row capacity; clamped to at least one.
    void init(std::size_t capacity = default_capacity);

    /// @brief Fold one record into the table.
    /// @param record Record to group; synthetic and bus-idle records are ignored.
    /// @param now_ns UI monotonic time.
    /// @param anomaly_threshold_ns Period outlier threshold.
    /// @return Updated row, or `nullptr` for non-row records.
    OverwriteRow* update(const CanRecordView& record, std::uint64_t now_ns,
                         std::int64_t anomaly_threshold_ns = 0) noexcept;

    /// @brief Look up one row.
    /// @param key Row identity.
    /// @return Borrowed row or `nullptr`.
    [[nodiscard]] const OverwriteRow* find(const OverwriteKey& key) const noexcept;
    /// @brief Look up one row.
    /// @param key Row identity.
    /// @return Borrowed row or `nullptr`.
    [[nodiscard]] OverwriteRow* find(const OverwriteKey& key) noexcept;

    /// @brief Row storage in LRU insertion/reuse order.
    /// @return Borrowed rows.
    [[nodiscard]] const std::vector<OverwriteRow>& rows() const noexcept { return rows_; }

    /// @brief Disable the table to simulate an allocation failure (tests only).
    void disable_for_tests() noexcept {
        rows_.clear();
        slots_.clear();
        disabled_ = true;
    }

    /// @brief Number of rows.
    /// @return Row count.
    [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }
    /// @brief Row capacity.
    /// @return Capacity.
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    /// @brief Number of evictions.
    /// @return Eviction counter.
    [[nodiscard]] std::uint64_t evictions() const noexcept { return evictions_; }
    /// @brief Selected keys evicted since creation.
    /// @return Counter.
    [[nodiscard]] std::uint64_t selected_evicted() const noexcept { return selected_evicted_; }

    /// @brief Reset period statistics for one channel.
    /// @param channel_id Channel identity.
    void reset_channel(std::uint16_t channel_id) noexcept;
    /// @brief Reset every row's period statistics.
    void reset_all() noexcept;
    /// @brief Whether a gap reset is pending (the `Next gap` action).
    /// @return `true` after a reset until `consume_gap()`.
    [[nodiscard]] bool gap_pending() const noexcept { return gap_pending_; }
    /// @brief Consume the pending gap.
    /// @return `true` when a gap was pending.
    bool consume_gap() noexcept;

    /// @brief Select one key.
    /// @param key Key to select.
    void select_key(const OverwriteKey& key);
    /// @brief Toggle one key.
    /// @param key Key to toggle.
    /// @return New selection state.
    bool toggle_key(const OverwriteKey& key);
    /// @brief Whether a key is selected.
    /// @param key Key to test.
    /// @return `true` when selected.
    [[nodiscard]] bool is_key_selected(const OverwriteKey& key) const noexcept;
    /// @brief Clear the key selection.
    void clear_selection() noexcept;
    /// @brief Selected keys in click order.
    /// @return Borrowed vector.
    [[nodiscard]] const std::vector<OverwriteKey>& selected_keys() const noexcept {
        return selected_;
    }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the row and selection counts.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Expand the hash table for a row capacity.
    void resize_index();
    /// @brief Hash slot index for a key.
    [[nodiscard]] std::size_t probe(const OverwriteKey& key) const noexcept;
    /// @brief Insert one row index into the hash table.
    void index_insert(std::size_t row_index);
    /// @brief Remove one key from the hash table.
    void index_erase(const OverwriteKey& key) noexcept;
    /// @brief Evict the least-recently-touched row.
    /// @return Row index reused by the caller.
    std::size_t evict_lru();
    /// @brief Drop a key from the selection list.
    void drop_selected(const OverwriteKey& key) noexcept;

    std::vector<OverwriteRow> rows_{};     ///< Row storage.
    std::vector<std::int32_t> slots_{};    ///< Open-addressing index.
    std::size_t capacity_{0};              ///< Row capacity.
    std::uint64_t touch_{0};               ///< LRU clock.
    std::uint64_t evictions_{0};           ///< Eviction count.
    std::uint64_t selected_evicted_{0};    ///< Evicted selected keys.
    bool gap_pending_{false};              ///< Reset pending.
    bool disabled_{false};                 ///< Test-only disabled table.
    std::vector<OverwriteKey> selected_{}; ///< Selected keys.
};

} // namespace nexigen::canview
