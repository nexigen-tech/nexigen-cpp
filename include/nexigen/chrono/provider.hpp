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

/// @brief The threadless clock authority over a fixed bank of calibrated stampers.
///
/// Owns a compile-time-sized bank of per-source `StampSource` objects, issues
/// them to producers on the cold path, samples the signed UTC offset, and
/// delivers UTC-offset snapshots to a bound sink only when triggered. It holds
/// no background thread: daemon mode is an optional `ChronoWorker` wrapper,
/// manual mode is the bare provider.
///
/// HOT/COLD SPLIT: a producer's hot path calls only the issued stamper's
/// `sample()` (`__rdtsc()` plus anchor arithmetic and one local store — no
/// atomic, no wall clock, no allocation, and no call back into the provider).
/// `prepare()`, `acquire()`, the UTC sampling helpers, `snapshot_now()`, and
/// `flush()` are cold. The provider mutex serializes manual and worker snapshot
/// delivery only; it is never taken by a producer stamp.
///
/// Bank capacity is a template parameter so steady state never reallocates and
/// sizing is per-instantiation; overdraft is a modeled `ResourceExhausted`,
/// never silent growth.
///
/// @tparam MaxSources Fixed number of stampers the bank can issue; defaults to
///         64. It must be large enough for every producer that will acquire.
/// @note Spec `chrono/provider`, requirement "Provider construction and sink
///       binding"; requirement "Fixed stamper bank capacity"; requirement
///       "Steady-state allocation of the authority".
template <std::size_t MaxSources = 64> class Provider {
public:
    /// @brief The per-source stamper type issued to producers.
    using Stamper = StampSource;
    /// @brief The compile-time bank capacity (`MaxSources`), for diagnostics.
    static constexpr std::size_t max_sources = MaxSources;

    /// @brief Bind a sink and copy the configuration; start no thread.
    ///
    /// The sink is type-erased through plain function pointers. Construction
    /// does not start a thread and delivers no snapshot; `snapshot_now()` and
    /// the UTC authority are usable immediately.
    ///
    /// @tparam Sink Concrete sink type, constrained by `concepts::Sink`.
    /// @param config Provider configuration copied into the instance.
    /// @param sink Sink that receives snapshots; it must outlive the provider.
    /// @note Spec `chrono/provider`, requirement "Provider construction and sink
    ///       binding".
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

    /// @brief Copying is deleted: the bank is fixed in place.
    Provider(const Provider&) = delete;
    /// @brief Copy assignment is deleted: the bank is fixed in place.
    Provider& operator=(const Provider&) = delete;
    /// @brief Moving is deleted: issued stamper addresses must stay stable.
    Provider(Provider&&) = delete;
    /// @brief Move assignment is deleted: issued stamper addresses must stay stable.
    Provider& operator=(Provider&&) = delete;

    /// @brief Flush the bound sink and release the bank storage.
    ///
    /// Delivers no hidden final snapshot (final delivery is a
    /// `ChronoWorker` shutdown duty) and never throws.
    ///
    /// @note Spec `chrono/provider`, requirement "Provider shutdown".
    ~Provider() {
        if (binding_.flush != nullptr) {
            static_cast<void>(binding_.flush(binding_.self));
        }
    }

    /// @brief Calibrate every bank source on the cold calling thread.
    ///
    /// The first calibration failure is returned and leaves the bank
    /// unprepared, so a later retry may succeed. On success the prepared flag
    /// is published with release semantics for `acquire()`'s acquire load.
    ///
    /// @return Success when every source calibrated, otherwise the first
    ///         sampleable clock error (typically `core::ErrorCode::SystemError`).
    /// @note Spec `chrono/provider`, requirement "Calibration gates issuance".
    ///       Cold path only; allocates nothing.
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

    /// @brief Issue one pre-calibrated per-source stamper (cold path).
    ///
    /// The slot index is claimed with a relaxed `fetch_add`: the counter only
    /// provides uniqueness, and the stamper data itself is published by the
    /// release store / acquire load on `prepared_`. An overdraft still consumes
    /// an index but reports exhaustion.
    ///
    /// @return A pointer to the next bank slot on success,
    ///         `core::ErrorCode::NotInitialized` before a completed
    ///         `prepare()`, or `core::ErrorCode::ResourceExhausted` when the
    ///         bank is fully issued.
    /// @note Spec `chrono/provider`, requirement "Stamper concept and
    ///       issuance"; requirement "Fixed stamper bank capacity".
    /// @warning The returned pointer is owned by the provider and stays valid
    ///          only for the provider's lifetime.
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

    /// @brief Sample a fresh signed `realtime` minus `monotonic_raw` offset.
    ///
    /// Two clock reads, no allocation, evaluated per call rather than cached.
    ///
    /// @return The signed offset in nanoseconds, or `0` when a clock read fails.
    /// @note Spec `chrono/provider`, requirement "UTC offset authority".
    ///       Cold path only.
    [[nodiscard]] std::int64_t utc_offset() noexcept {
        std::int64_t offset = 0;
        static_cast<void>(sample_utc_offset(offset));
        return offset;
    }

    /// @brief Current wall time as nanoseconds since the Unix epoch.
    ///
    /// Reads the monotonic clock once and composes it with a fresh
    /// `utc_offset()` through `utc_from_mono_ns`.
    ///
    /// @return UTC nanoseconds, or `0` when the monotonic clock read fails.
    /// @note Spec `chrono/provider`, requirement "UTC offset authority".
    ///       Cold path only.
    [[nodiscard]] std::int64_t utc_now_ns() noexcept {
        const core::Result<Timestamp> mono = Timestamp::now();
        if (!mono) {
            return 0;
        }
        return utc_from_mono_ns(mono->nanoseconds(), utc_offset());
    }

    /// @brief Sample stamp and offset at one instant and deliver one snapshot.
    ///
    /// Delivery happens synchronously on the calling thread before the call
    /// returns. The snapshot mutex serializes this path against the worker's
    /// delivery so complete snapshots never interleave. Failures leave the
    /// provider usable for later calls.
    ///
    /// @return Success when the snapshot was sampled and accepted by the sink
    ///         (or no sink is bound), otherwise `core::ErrorCode::SystemError`
    ///         for a clock failure or the sink's own failure status.
    /// @note Spec `chrono/provider`, requirement "Manual snapshot delivery";
    ///       requirement "Concurrent manual triggers never interleave".
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

    /// @brief Flush the bound sink.
    ///
    /// @return Success when no sink is bound or the sink flushed, otherwise the
    ///         sink's failure status.
    /// @note Spec `chrono/provider`, requirement "Manual snapshot delivery".
    [[nodiscard]] core::Status flush() noexcept {
        if (binding_.flush == nullptr) {
            return {};
        }
        return binding_.flush(binding_.self);
    }

    /// @brief Render configuration and counting state as a diagnostic string.
    ///
    /// Reports interval, kind, worker spec, prepared flag, issued count, and
    /// capacity. Both atomic loads are relaxed because the text is a
    /// diagnostic snapshot, not a synchronization point. Allocates through
    /// `std::format`.
    ///
    /// @return The rendered text.
    /// @note Spec `chrono/provider`, requirement "Provider diagnostics"; project
    ///       mandate 9.
    [[nodiscard]] std::string to_string() const {
        return std::format(
            "Provider(collect_interval_ns={}, kind={}, worker_thread={}, "
            "prepared={}, issued={}, capacity={})",
            config_.collect_interval.count(), nexigen::chrono::to_string(config_.kind),
            config_.worker_thread.to_string(), prepared_.load(std::memory_order_relaxed),
            next_free_.load(std::memory_order_relaxed), MaxSources);
    }

private:
    /// @brief Bind a configuration and a prepared sink binding.
    ///
    /// @param config Provider configuration copied into the instance.
    /// @param binding Type-erased sink thunks; `self` must outlive the provider.
    Provider(const ProviderConfig& config, detail::SinkBinding binding)
        : config_(config), binding_(binding) {}

    alignas(64) std::array<StampSource, MaxSources> bank_{}; ///< Fixed stamper storage.
    std::atomic<std::size_t> next_free_{0};       ///< Next bank slot; relaxed, uniqueness only.
    std::atomic<bool> prepared_{false};           ///< Release/acquire gate for calibration.
    std::atomic<bool> calibration_failed_{false}; ///< Sticky cold-path failure flag.
    ProviderConfig config_{};                     ///< Copied construction configuration.
    detail::SinkBinding binding_{};               ///< Type-erased sink thunks.
    std::mutex snapshot_mutex_{}; ///< Serializes manual + worker delivery (cold only).
};

static_assert(concepts::Provider<Provider<>>, "Provider must satisfy the provider concept");

} // namespace nexigen::chrono

/// @brief `std::formatter` specialization rendering the provider diagnostically.
///
/// @tparam MaxSources Fixed stamper-bank capacity of the provider being formatted.
/// @note Spec `chrono/provider`, requirement "Provider diagnostics"; project
///       mandate 11.
template <std::size_t MaxSources>
struct std::formatter<nexigen::chrono::Provider<MaxSources>> : std::formatter<std::string_view> {
    /// @brief Format a provider as its diagnostic string.
    ///
    /// @tparam Ctx Format context type; the inherited string specifiers apply.
    /// @param provider Provider to format.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    /// @note Project mandate 11 (`std::formatter`).
    template <class Ctx>
    auto format(const nexigen::chrono::Provider<MaxSources>& provider, Ctx& ctx) const {
        const std::string text = provider.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
