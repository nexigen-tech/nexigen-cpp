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

class MetricAggregator {
public:
    explicit MetricAggregator(std::uint32_t capacity) : slots_(capacity) {}

    /// Record the latest frame for its key (insert or overwrite).
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

    /// Visit each occupied slot (the drain thread is the only writer).
    template <class Fn>
    void for_each(Fn&& fn) const {
        for (const Slot& slot : slots_) {
            if (slot.used) {
                fn(slot.frame);
            }
        }
    }

    [[nodiscard]] std::uint32_t size() const noexcept { return used_; }

private:
    struct Slot {
        bool used{false};
        MetricFrame frame{};
    };

    static bool same_key(const MetricFrame& a, const MetricFrame& b) noexcept {
        return std::memcmp(a.app_id, b.app_id, sizeof(a.app_id)) == 0 &&
               std::memcmp(a.record.identity.meter_name, b.record.identity.meter_name,
                           sizeof(a.record.identity.meter_name)) == 0 &&
               std::memcmp(a.record.identity.metric_name, b.record.identity.metric_name,
                           sizeof(a.record.identity.metric_name)) == 0;
    }

    [[nodiscard]] std::size_t home_index(const MetricFrame& frame) const noexcept {
        return hash(frame) % slots_.size();
    }

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
