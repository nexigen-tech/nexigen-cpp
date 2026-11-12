// nexigen::metrics — latest-wins aggregation table for the MetricsDaemon
// (change add-metrics-daemon; spec metrics/metrics-daemon "Daemon aggregates
// the latest record per metric"; design D4).
//
// Single-writer (the drain thread), pre-allocated, open-addressing. Keyed by
// (app_id, meter_name, metric_name); each slot holds the full MetricFrame.
// No atomics, no heap growth, no locks on the update path.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include <nexigen/metrics/metric_frame.hpp>

namespace nexigen::metrics::detail {

/// @brief Latest-wins aggregation table keyed by `(app, meter, metric)`.
///
/// Single-writer (the drain thread), pre-allocated and open-addressing: each
/// occupied slot holds the full `MetricFrame` for its key, and an update either
/// inserts or overwrites that frame. No atomics, no heap growth and no locks on
/// the update path, so a scrape always reflects the most recent record per key.
///
/// @note Spec `metrics/metrics-daemon`, requirement "Daemon aggregates the
///       latest record per metric".
class MetricAggregator {
public:
    /// @brief Pre-allocate the slot table.
    ///
    /// @param capacity Number of slots; `0` makes `update` a no-op.
    explicit MetricAggregator(std::uint32_t capacity) : slots_(capacity) {}

    /// @brief Record the latest frame for its key (insert or overwrite).
    ///
    /// Hashes the key, then linear-probes until it finds an empty slot (insert)
    /// or the matching key (overwrite). A completely full table overwrites the
    /// frame in the key's home slot rather than dropping the record silently.
    ///
    /// @param frame Latest frame for its `(app_id, meter_name, metric_name)` key.
    /// @warning Single-writer: must run only on the drain thread, with no
    ///          concurrent `for_each`.
    /// @note Spec `metrics/metrics-daemon`, requirement "Daemon aggregates the
    ///       latest record per metric".
    void update(const MetricFrame& frame) noexcept {
        const std::size_t cap = slots_.size();
        if (cap == 0U) {
            return;
        }
        std::size_t index = home_index(frame);
        for (std::size_t probe = 0; probe < cap; ++probe) {
            Slot& slot = slots_[index];
            if (!slot.used) {
                slot.used = true;
                slot.frame = frame;
                ++used_;
                return;
            }
            if (same_key(slot.frame, frame)) {
                slot.frame = frame;
                return;
            }
            index = (index + 1U) % cap;
        }
        // Table full: overwrite the home slot rather than dropping silently.
        slots_[home_index(frame)].frame = frame;
    }

    /// @brief Visit each occupied slot in table order.
    ///
    /// @tparam Fn Callable invocable as `fn(const MetricFrame&)`.
    /// @param fn Visitor applied once per occupied slot; iteration order is the
    ///        hash-table order, not insertion order.
    /// @note Spec `metrics/metrics-daemon`, requirement "Daemon aggregates the
    ///       latest record per metric"; only the drain thread may write while
    ///       this runs.
    template <class Fn> void for_each(Fn&& fn) const {
        for (const Slot& slot : slots_) {
            if (slot.used) {
                fn(slot.frame);
            }
        }
    }

    /// @brief Count the occupied slots.
    ///
    /// @return Number of distinct keys currently held.
    [[nodiscard]] std::uint32_t size() const noexcept { return used_; }

private:
    /// One open-addressing cell: occupancy flag plus the latest frame.
    struct Slot {
        /// Whether this cell currently holds a key.
        bool used{false};
        /// Latest frame for the key; meaningful only when `used` is set.
        MetricFrame frame{};
    };

    /// @brief Compare the aggregation keys of two frames.
    ///
    /// @param a First frame.
    /// @param b Second frame.
    /// @return `true` when `app_id`, meter name and metric name all match.
    static bool same_key(const MetricFrame& a, const MetricFrame& b) noexcept {
        return std::memcmp(a.app_id, b.app_id, sizeof(a.app_id)) == 0 &&
               std::memcmp(a.record.identity.meter_name, b.record.identity.meter_name,
                           sizeof(a.record.identity.meter_name)) == 0 &&
               std::memcmp(a.record.identity.metric_name, b.record.identity.metric_name,
                           sizeof(a.record.identity.metric_name)) == 0;
    }

    /// @brief Compute the home slot index for a frame's key.
    ///
    /// @param frame Frame whose key is hashed.
    /// @return `hash(frame) % slots_.size()`; only valid when the table is
    ///         non-empty.
    [[nodiscard]] std::size_t home_index(const MetricFrame& frame) const noexcept {
        return hash(frame) % slots_.size();
    }

    /// @brief FNV-1a hash over the key bytes of a frame.
    ///
    /// @param frame Frame whose `app_id`, meter name and metric name are mixed.
    /// @return 64-bit hash of the aggregation key.
    [[nodiscard]] static std::uint64_t hash(const MetricFrame& frame) noexcept {
        // FNV-1a over the key bytes.
        std::uint64_t h = 1469598103934665603ULL;
        const auto mix = [&h](const char* data, std::size_t size) {
            for (std::size_t i = 0; i < size; ++i) {
                h ^= static_cast<std::uint8_t>(data[i]);
                h *= 1099511628211ULL;
            }
        };
        mix(frame.app_id, sizeof(frame.app_id));
        mix(frame.record.identity.meter_name, sizeof(frame.record.identity.meter_name));
        mix(frame.record.identity.metric_name, sizeof(frame.record.identity.metric_name));
        return h;
    }

    std::vector<Slot> slots_;
    std::uint32_t used_{0};
};

} // namespace nexigen::metrics::detail
