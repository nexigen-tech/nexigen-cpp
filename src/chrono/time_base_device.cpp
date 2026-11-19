// nexigen::chrono — simulated and manual hardware time-domain backends (change
// add-chrono-service-contract; design D-F).
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <nexigen/chrono/time_base_device.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/integrity/crc32c.hpp>

namespace nexigen::chrono {
namespace {

/// @brief Read `CLOCK_MONOTONIC_RAW` at the current instant.
///
/// @return Nanoseconds since boot, or zero on failure.
[[nodiscard]] std::uint64_t now_raw_ns() noexcept {
    const auto now = Timestamp::now();
    return now ? now->nanoseconds() : 0U;
}

} // namespace

SimulatedTimeBaseDevice::SimulatedTimeBaseDevice(std::uint16_t domain_id, std::uint64_t epoch,
                                                 std::uint64_t ticks_per_ns_q32,
                                                 std::uint64_t base_ticks,
                                                 std::uint64_t latency_ns) noexcept
    : domain_id_(domain_id), epoch_(epoch),
      ticks_per_ns_q32_(ticks_per_ns_q32 == 0U ? 1ULL << 32U : ticks_per_ns_q32),
      base_ticks_(base_ticks), latency_ns_(latency_ns) {
    registers_.version = 1U;
    registers_.caps = 0x7U; // PTM + latch + PHC
    for (std::size_t i = 0; i < registers_.uuid.size(); ++i) {
        registers_.uuid[i] = static_cast<std::uint8_t>(0x10U + i);
    }
    registers_.lock_state = 1U;
    registers_.sync_epoch = static_cast<std::uint16_t>(epoch & 0xFFFFU);
    registers_.last_exchange_dev_ticks = base_ticks;
    base_canonical_ns_ = static_cast<std::int64_t>(now_raw_ns());
    registers_.ptm_ticks = static_cast<std::uint64_t>(base_canonical_ns_);
}

void SimulatedTimeBaseDevice::advance_pair() noexcept {
    ++reads_;
    const std::uint64_t ticks = base_ticks_ + reads_ * 1'000U;
    const std::uint64_t delta_ticks = ticks - base_ticks_;
    const std::uint64_t delta_ns = static_cast<std::uint64_t>(nexigen::core::detail::mul_shift_i64(
        static_cast<std::int64_t>(delta_ticks), ticks_per_ns_q32_, 32U));
    registers_.dev_ticks = ticks;
    registers_.ptm_ticks = static_cast<std::uint64_t>(base_canonical_ns_) + delta_ns + latency_ns_;
    registers_.pair_sequence = static_cast<std::uint32_t>(reads_);
    registers_.last_exchange_dev_ticks = ticks;
    registers_.prop_delay_ns = static_cast<std::uint32_t>(latency_ns_);
    registers_.residual_ns = static_cast<std::uint32_t>(latency_ns_ / 2U);
    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&registers_);
    registers_.pair_crc = core::integrity::crc32c(
        std::span<const std::uint8_t>(bytes, offsetof(DeviceRegisterMap, pair_crc)));
}

bool SimulatedTimeBaseDevice::read_pair(TimeBaseCorrelationPair& out) noexcept {
    advance_pair();
    out.native_ticks = registers_.dev_ticks;
    out.canonical_ns = static_cast<std::int64_t>(registers_.ptm_ticks);
    out.read_latency_ns = 0U;
    out.uncertainty_ns = registers_.residual_ns != 0U ? registers_.residual_ns : 1U;
    out.latched = true;
    out.ptm_capable = true;
    return true;
}

bool SimulatedTimeBaseDevice::snapshot_registers(DeviceRegisterMap& out) noexcept {
    out = registers_;
    return true;
}

bool SimulatedTimeBaseDevice::snapshot_thunk(void* self, DeviceRegisterMap& out) noexcept {
    return static_cast<SimulatedTimeBaseDevice*>(self)->snapshot_registers(out);
}

void SimulatedTimeBaseDevice::advance_thunk(void* self) noexcept {
    static_cast<SimulatedTimeBaseDevice*>(self)->advance_pair();
}

bool SimulatedTimeBaseDevice::read_thunk(void* self, TimeBaseCorrelationPair& out) noexcept {
    return static_cast<SimulatedTimeBaseDevice*>(self)->read_pair(out);
}

TimeBaseDeviceBackend SimulatedTimeBaseDevice::backend() noexcept {
    return TimeBaseDeviceBackend{this,        &SimulatedTimeBaseDevice::read_thunk,
                                 domain_id_,  epoch_,
                                 "simulated", DeviceAccessMode::RegisterPair};
}

RegisterPairDevice SimulatedTimeBaseDevice::register_pair_backend() noexcept {
    return RegisterPairDevice{domain_id_,
                              epoch_,
                              "simulated",
                              this,
                              &SimulatedTimeBaseDevice::snapshot_thunk,
                              &SimulatedTimeBaseDevice::advance_thunk};
}

bool RegisterPairDevice::read_pair(TimeBaseCorrelationPair& out) noexcept {
    if (snapshot_ == nullptr || self_ == nullptr) {
        return false;
    }
    if (latch_ != nullptr) {
        latch_(self_);
    }
    DeviceRegisterMap first{};
    DeviceRegisterMap second{};
    if (!snapshot_(self_, first) || !snapshot_(self_, second)) {
        return false;
    }
    if (first.magic != DeviceRegisterMap{}.magic || first.pair_sequence != second.pair_sequence) {
        return false;
    }
    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&first);
    if (core::integrity::crc32c(std::span<const std::uint8_t>(
            bytes, offsetof(DeviceRegisterMap, pair_crc))) != first.pair_crc) {
        return false;
    }
    if (has_sequence_ && first.pair_sequence == last_sequence_) {
        return false; // stale: no new latch edge since the last accepted pair
    }
    if (first.dev_ticks == 0U || first.ptm_ticks == 0U) {
        return false;
    }
    last_sequence_ = first.pair_sequence;
    has_sequence_ = true;
    out.native_ticks = first.dev_ticks;
    out.canonical_ns = static_cast<std::int64_t>(first.ptm_ticks);
    out.read_latency_ns = 0U;
    out.uncertainty_ns = first.residual_ns != 0U ? first.residual_ns : 1U;
    out.latched = true;
    out.ptm_capable = (first.caps & 0x1U) != 0U;
    return true;
}

bool RegisterPairDevice::read_thunk(void* self, TimeBaseCorrelationPair& out) noexcept {
    return static_cast<RegisterPairDevice*>(self)->read_pair(out);
}

TimeBaseDeviceBackend RegisterPairDevice::backend() noexcept {
    return TimeBaseDeviceBackend{this,  &RegisterPairDevice::read_thunk, domain_id_, epoch_,
                                 name_, DeviceAccessMode::RegisterPair};
}

SimulatedPhcDevice::SimulatedPhcDevice(std::uint16_t domain_id, std::uint64_t epoch,
                                       std::uint64_t ticks_per_ns_q32, std::uint64_t base_ticks,
                                       std::uint64_t latency_ns) noexcept
    : domain_id_(domain_id), epoch_(epoch),
      ticks_per_ns_q32_(ticks_per_ns_q32 == 0U ? 1ULL << 32U : ticks_per_ns_q32),
      base_ticks_(base_ticks), latency_ns_(latency_ns) {
    base_canonical_ns_ = static_cast<std::int64_t>(now_raw_ns());
}

bool SimulatedPhcDevice::read_pair(TimeBaseCorrelationPair& out) noexcept {
    ++reads_;
    const std::uint64_t ticks = base_ticks_ + reads_ * 1'000U;
    const std::uint64_t delta_ticks = ticks - base_ticks_;
    const std::uint64_t delta_ns = static_cast<std::uint64_t>(nexigen::core::detail::mul_shift_i64(
        static_cast<std::int64_t>(delta_ticks), ticks_per_ns_q32_, 32U));
    out.native_ticks = ticks;
    out.canonical_ns = base_canonical_ns_ + static_cast<std::int64_t>(delta_ns) +
                       static_cast<std::int64_t>(latency_ns_);
    out.read_latency_ns = 0U;
    out.uncertainty_ns = latency_ns_ / 2U + 1U;
    out.latched = true;
    out.ptm_capable = false;
    return true;
}

bool SimulatedPhcDevice::read_thunk(void* self, TimeBaseCorrelationPair& out) noexcept {
    return static_cast<SimulatedPhcDevice*>(self)->read_pair(out);
}

TimeBaseDeviceBackend SimulatedPhcDevice::backend() noexcept {
    return TimeBaseDeviceBackend{this,        &SimulatedPhcDevice::read_thunk,
                                 domain_id_,  epoch_,
                                 "simulated", DeviceAccessMode::PhcCrossTimestamp};
}

bool ManualPairDevice::read_pair(TimeBaseCorrelationPair& out) noexcept {
    if (counter_ == nullptr || raw_clock_ == nullptr) {
        return false;
    }
    const std::uint64_t before = raw_clock_(self_);
    const std::uint64_t ticks = counter_(self_);
    const std::uint64_t after = raw_clock_(self_);
    if (before == 0U || after < before) {
        return false;
    }
    const std::uint64_t window = after - before;
    out.native_ticks = ticks;
    out.canonical_ns = static_cast<std::int64_t>(before + window / 2U);
    out.read_latency_ns = window;
    out.uncertainty_ns = window / 2U + 1U;
    out.latched = false;
    out.ptm_capable = false;
    return true;
}

bool ManualPairDevice::read_thunk(void* self, TimeBaseCorrelationPair& out) noexcept {
    return static_cast<ManualPairDevice*>(self)->read_pair(out);
}

TimeBaseDeviceBackend ManualPairDevice::backend() noexcept {
    return TimeBaseDeviceBackend{this,  &ManualPairDevice::read_thunk, domain_id_, epoch_,
                                 name_, DeviceAccessMode::ManualPair};
}

} // namespace nexigen::chrono
