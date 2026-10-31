// nexigen::chrono — provider implementation: sampling, the background worker,
// control plane and shutdown (change add-chrono-clock-service). See
// chrono_provider.hpp for the contract.
#include <cstdint>
#include <string>

#include <ctime>

#include <nexigen/chrono/chrono_provider.hpp>
#include <nexigen/chrono/detail/affinity.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

ChronoProvider::ChronoProvider(const ChronoProvider_config& config, detail::Sink_binding binding)
    : config_(config), binding_(binding) {
    if (config_.collect_interval > std::chrono::nanoseconds{0}) {
        worker_ = std::jthread([this](std::stop_token stop) { snapshot_loop(stop); });
    }
}

ChronoProvider::~ChronoProvider() {
    // Stop the worker first: no snapshot may run while the provider is being
    // torn down.
    if (worker_.joinable()) {
        worker_.request_stop();
        {
            std::lock_guard<std::mutex> lock(wake_mutex_);
            wake_cv_.notify_all();
        }
        worker_.join();
    }

    // Deliver one final snapshot and flush so the last interval is not lost.
    static_cast<void>(snapshot_now());
    if (binding_.flush != nullptr) {
        static_cast<void>(binding_.flush(binding_.self));
    }
}

core::Status ChronoProvider::snapshot_now() noexcept {
    // Serialize with the background worker so the sink is never written by two
    // threads at once (the worker and a manual caller both enter here).
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    const core::Result<chrono::Timestamp> stamp = chrono::Timestamp::now();
    if (!stamp) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    ::timespec realtime{};
    if (::clock_gettime(CLOCK_REALTIME, &realtime) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    const std::int64_t rt_ns = static_cast<std::int64_t>(realtime.tv_sec) * 1'000'000'000LL +
                               static_cast<std::int64_t>(realtime.tv_nsec);
    const Chrono_snapshot snapshot{*stamp, rt_ns - static_cast<std::int64_t>(stamp->nanoseconds())};

    if (binding_.on_snapshot == nullptr) {
        return {};
    }
    return binding_.on_snapshot(binding_.self, snapshot);
}

core::Status ChronoProvider::flush() noexcept {
    if (binding_.flush == nullptr) {
        return {};
    }
    return binding_.flush(binding_.self);
}

core::Status ChronoProvider::startup_status() const noexcept {
    const core::ErrorCode error = startup_error_.load(std::memory_order_acquire);
    if (error != core::ErrorCode::Ok) {
        return std::unexpected(error);
    }
    return {};
}

std::uint64_t ChronoProvider::worker_mask() const noexcept {
    return worker_mask_.load(std::memory_order_relaxed);
}

std::string ChronoProvider::to_string() const {
    return "ChronoProvider(collect_interval_ns=" +
           std::to_string(config_.collect_interval.count()) +
           ", core_mask=0x" + std::to_string(config_.core_mask) +
           ", worker_mask=0x" + std::to_string(worker_mask()) + ")";
}

void ChronoProvider::snapshot_loop(std::stop_token stop) noexcept {
    // Pin this thread before the first snapshot: the worker must not preempt
    // the real-time threads.
    if (config_.core_mask != 0U) {
        const core::Status pinned = detail::pin_self(config_.core_mask);
        if (!pinned) {
            startup_error_.store(pinned.error(), std::memory_order_release);
        }
    }
    std::uint64_t mask = 0;
    if (detail::self_affinity(mask)) {
        worker_mask_.store(mask, std::memory_order_relaxed);
    }

    auto next = std::chrono::steady_clock::now() + config_.collect_interval;
    while (!stop.stop_requested()) {
        {
            std::unique_lock<std::mutex> lock(wake_mutex_);
            wake_cv_.wait_until(lock, stop, next, [] { return false; });
        }
        if (stop.stop_requested()) {
            break;
        }
        static_cast<void>(snapshot_now());
        next += config_.collect_interval;
        const auto now = std::chrono::steady_clock::now();
        if (next < now) {
            next = now + config_.collect_interval; // do not chase a backlog
        }
    }
}

// --- ChronoProvider_config serialization (cold path).

std::string ChronoProvider_config::to_string() const {
    return "ChronoProvider_config(collect_interval_ns=" +
           std::to_string(collect_interval.count()) +
           ", core_mask=0x" + std::to_string(core_mask) + ")";
}

std::string to_string(const ChronoProvider_config& config) { return config.to_string(); }

void to_json(nlohmann::json& j, const ChronoProvider_config& config) {
    j = nlohmann::json{
        {"collect_interval_ns", config.collect_interval.count()},
        {"core_mask", config.core_mask},
    };
}

void from_json(const nlohmann::json& j, ChronoProvider_config& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto interval_it = j.find("collect_interval_ns");
    if (interval_it != j.end() && interval_it->is_number_integer()) {
        const std::int64_t interval = interval_it->get<std::int64_t>();
        if (interval >= 0) {
            out.collect_interval = std::chrono::nanoseconds{interval};
        }
    }
    const auto mask_it = j.find("core_mask");
    if (mask_it != j.end() && mask_it->is_number_unsigned()) {
        out.core_mask = mask_it->get<std::uint64_t>();
    }
}

} // namespace nexigen::chrono
