// nexigen::chrono — the threadless clock authority (change
// add-clock-authority-core; spec chrono/provider).
//
// Provider<MaxSources> owns the calibrated per-source stampers issued to
// producers alone, samples the signed UTC offset on the cold path, and
// delivers UTC-offset snapshots to a bound sink only when triggered. It holds
// no background thread: daemon mode is an optional ChronoWorker wrapper, manual
// mode is the bare provider.
//
// Bank capacity is a compile-time template parameter so steady state never
// reallocates and sizing is a per-instantiation decision; overdraft is a
// modeled ResourceExhausted, never silent growth.
//
// HOT PATH: an issued stamper's sample() stays on the per-source instrument
// (__rdtsc() + anchor arithmetic + one local store — no atomic, no wall clock,
// no allocation, no call through the provider). The provider's own mutex is
// cold-path only: it serializes manual and worker snapshot delivery, never a
// producer stamp.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <format>
#include <mutex>
#include <string>

#include <nexigen/chrono/concepts/provider_concept.hpp>
#include <nexigen/chrono/concepts/sink_concept.hpp>
#include <nexigen/chrono/concepts/stamper_concept.hpp>
#include <nexigen/chrono/detail/sink_binding.hpp>
#include <nexigen/chrono/provider_config.hpp>
#include <nexigen/chrono/snapshot.hpp>
#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/chrono/utc_offset.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// The threadless clock authority.
template <std::size_t MaxSources = 64> class Provider {
public:
    using Stamper = StampSource;
    static constexpr std::size_t max_sources = MaxSources;

    /// Bind a sink (type-erased through plain function pointers). Construction
    /// does NOT start any thread and delivers no snapshot. The sink MUST
    /// outlive the provider.
    template <concepts::Sink Sink>
    Provider(const ProviderConfig& config, Sink& sink)
        : Provider(config, detail::SinkBinding{
                               static_cast<void*>(&sink),
                               [](void* self, const Snapshot& snapshot) noexcept -> core::Status {
                                   return static_cast<Sink*>(self)->on_snapshot(snapshot);
                               },
                               [](void* self) noexcept -> core::Status {
                                   return static_cast<Sink*>(self)->flush();
                               }}) {}

    Provider(const Provider&) = delete;
    Provider& operator=(const Provider&) = delete;
    Provider(Provider&&) = delete;
    Provider& operator=(Provider&&) = delete;

    /// Flush the bound sink; delivers no hidden final snapshot (final delivery
    /// is a ChronoWorker shutdown duty).
    ~Provider() {
        if (binding_.flush != nullptr) {
            static_cast<void>(binding_.flush(binding_.self));
        }
    }

    /// Calibrate every bank source on the cold calling thread. On any
    /// calibration failure the bank stays unprepared (retryable) and the
    /// sampleable clock error is returned.
    [[nodiscard]] core::Status prepare() noexcept {
        core::ErrorCode first = core::ErrorCode::Ok;
        for (auto& source : bank_) {
            const core::Status calibrated = source.calibrate();
            if (!calibrated && first == core::ErrorCode::Ok) {
                first = calibrated.error();
            }
        }
        calibration_failed_.store(first != core::ErrorCode::Ok, std::memory_order_relaxed);
        prepared_.store(first == core::ErrorCode::Ok, std::memory_order_release);
        if (first != core::ErrorCode::Ok) {
            return std::unexpected(first);
        }
        return {};
    }

    /// Issue one pre-calibrated per-source stamper (cold path). Before a
    /// completed prepare() this reports NotInitialized; a fully issued bank
    /// reports ResourceExhausted. The provider outlives every issued source.
    [[nodiscard]] core::Result<Stamper*> acquire() noexcept {
        if (!prepared_.load(std::memory_order_acquire)) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        const std::size_t slot = next_free_.fetch_add(1, std::memory_order_relaxed);
        if (slot >= MaxSources) {
            return std::unexpected(core::ErrorCode::ResourceExhausted);
        }
        return &bank_[slot];
    }

    /// Fresh signed realtime-minus-monotonic-raw offset (two clock reads, cold
    /// path, no allocation). A clock failure yields a zero offset.
    [[nodiscard]] std::int64_t utc_offset() noexcept {
        std::int64_t offset = 0;
        static_cast<void>(sample_utc_offset(offset));
        return offset;
    }

    /// Current wall time as nanoseconds since the epoch (monotonic-raw plus the
    /// fresh UTC offset, composed through utc_from_mono_ns).
    [[nodiscard]] std::int64_t utc_now_ns() noexcept {
        const core::Result<Timestamp> mono = Timestamp::now();
        if (!mono) {
            return 0;
        }
        return utc_from_mono_ns(mono->nanoseconds(), utc_offset());
    }

    /// Sample stamp and offset at one instant and deliver exactly one snapshot
    /// to the bound sink on the calling thread before returning.
    [[nodiscard]] core::Status snapshot_now() noexcept {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        const core::Result<Timestamp> stamp = Timestamp::now();
        if (!stamp) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        ::timespec realtime{};
        if (::clock_gettime(CLOCK_REALTIME, &realtime) != 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        const std::int64_t rt_ns = static_cast<std::int64_t>(realtime.tv_sec) * 1'000'000'000LL +
                                   static_cast<std::int64_t>(realtime.tv_nsec);
        const Snapshot snapshot{*stamp, rt_ns - static_cast<std::int64_t>(stamp->nanoseconds())};
        if (binding_.on_snapshot == nullptr) {
            return {};
        }
        return binding_.on_snapshot(binding_.self, snapshot);
    }

    /// Flush the bound sink.
    [[nodiscard]] core::Status flush() noexcept {
        if (binding_.flush == nullptr) {
            return {};
        }
        return binding_.flush(binding_.self);
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format(
            "Provider(collect_interval_ns={}, kind={}, worker_thread={}, "
            "prepared={}, issued={}, capacity={})",
            config_.collect_interval.count(), nexigen::chrono::to_string(config_.kind),
            config_.worker_thread.to_string(), prepared_.load(std::memory_order_relaxed),
            next_free_.load(std::memory_order_relaxed), MaxSources);
    }

private:
    Provider(const ProviderConfig& config, detail::SinkBinding binding)
        : config_(config), binding_(binding) {}

    alignas(64) std::array<StampSource, MaxSources> bank_{};
    std::atomic<std::size_t> next_free_{0};
    std::atomic<bool> prepared_{false};
    std::atomic<bool> calibration_failed_{false};
    ProviderConfig config_{};
    detail::SinkBinding binding_{};
    std::mutex snapshot_mutex_{}; // serializes manual + worker snapshot delivery (cold only)
};

static_assert(concepts::Provider<Provider<>>, "Provider must satisfy the provider concept");

} // namespace nexigen::chrono

/// The provider formats as its diagnostic string (mandate 11).
template <std::size_t MaxSources>
struct std::formatter<nexigen::chrono::Provider<MaxSources>> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::Provider<MaxSources>& provider, Ctx& ctx) const {
        const std::string text = provider.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
