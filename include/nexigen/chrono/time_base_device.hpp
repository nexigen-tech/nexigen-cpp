// nexigen::chrono — hardware time-domain backend seam (change
// add-chrono-service-contract; spec chrono/time-base "Hardware domain access
// modes"; design D-F).
//
// The daemon never touches a device register or an ioctl directly. A backend
// returns one correlation pair `(native ticks, canonical ns)`; how the pair is
// obtained is the backend's business:
//
//  - `PhcCrossTimestamp`: a PTP hardware clock registered by a driver, read
//    through `PTP_SYS_OFFSET_PRECISE`/`_EXTENDED` (see
//    `phc_cross_timestamp.hpp`);
//  - `RegisterPair`: the latched D1-D3 register block read through UIO/VFIO or
//    the vendor character device (see docs/firmware-time-base-register-map.md);
//  - `ManualPair`: legacy modules without a latch, where the counter and
//    `CLOCK_MONOTONIC_RAW` are read in software and the read window becomes the
//    pair's latency and uncertainty.
//
// `SimulatedTimeBaseDevice`, `SimulatedPhcDevice`, and `ManualPairDevice`
// implement the three modes in memory or over caller-supplied readers so the
// estimator and the publication path can be exercised without hardware.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/device_access_mode.hpp>
#include <nexigen/chrono/time_base_estimator.hpp>

namespace nexigen::chrono {

/// @brief One hardware correlation pair.
///
/// `native_ticks` is the free-running device counter; `canonical_ns` is the hub
/// instant of the same hardware edge. No frequency or offset is applied by the
/// backend or the driver. `read_latency_ns` records the software read window
/// for a manual pair (zero for a latched or cross-timestamp pair), and
/// `uncertainty_ns` is the backend's one-sided uncertainty estimate.
struct TimeBaseCorrelationPair {
    std::uint64_t native_ticks{0};    ///< Raw device counter.
    std::int64_t canonical_ns{0};     ///< Hub instant of the same edge.
    std::uint64_t read_latency_ns{0}; ///< Software read window (manual pairs).
    std::uint64_t uncertainty_ns{0};  ///< One-sided uncertainty of the pair.
    bool latched{false};              ///< Pair came from a hardware latch.
    bool ptm_capable{false};          ///< Device supports PCIe PTM.
};

/// @brief Read callback of a device backend.
///
/// @param self Backend state.
/// @param out Destination pair.
/// @return `true` when a fresh pair was produced.
using DeviceReadPair = bool (*)(void*, TimeBaseCorrelationPair&) noexcept;

/// @brief Type-erased device backend (plain function pointer, no RTTI).
///
/// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
class TimeBaseDeviceBackend {
public:
    /// @brief Bind one backend implementation.
    ///
    /// @param self Opaque backend state; it must outlive this object.
    /// @param reader Pair reader.
    /// @param domain_id Time-base domain id this backend publishes.
    /// @param epoch Domain epoch of the current device lifecycle.
    /// @param name Human-readable device name (borrowed).
    /// @param mode Access mode used by the backend.
    TimeBaseDeviceBackend(void* self, DeviceReadPair reader, std::uint16_t domain_id,
                          std::uint64_t epoch, std::string_view name,
                          DeviceAccessMode mode) noexcept
        : self_(self), read_(reader), domain_id_(domain_id), epoch_(epoch), name_(name),
          mode_(mode) {}

    /// @brief Construct an empty backend that is not callable.
    TimeBaseDeviceBackend() noexcept = default;

    /// @brief Whether the backend is callable.
    /// @return `true` when both the state and the reader are present.
    [[nodiscard]] bool valid() const noexcept { return self_ != nullptr && read_ != nullptr; }

    /// @brief Read one fresh correlation pair.
    /// @param out Destination pair.
    /// @return `true` when the backend produced a pair.
    [[nodiscard]] bool read(TimeBaseCorrelationPair& out) noexcept {
        return valid() && read_(self_, out);
    }

    /// @brief Domain id this backend publishes.
    /// @return Id in `device_domain_base..device_domain_last`.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] std::uint16_t domain_id() const noexcept { return domain_id_; }
    /// @brief Domain epoch this backend publishes.
    /// @return Epoch of the current device lifecycle.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }
    /// @brief Human-readable device name.
    /// @return Borrowed name stamped into the published row.
    /// @note Spec `chrono/time-base`, requirement "Domain coefficient row".
    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    /// @brief Access mode.
    /// @return Mode that produced the pair.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] DeviceAccessMode mode() const noexcept { return mode_; }

    /// @brief Feed one read pair into an estimator.
    /// @param estimator Estimator to update.
    /// @return `true` when a pair was read and added.
    [[nodiscard]] bool sample_into(TimeBaseEstimator& estimator) noexcept {
        TimeBaseCorrelationPair pair{};
        if (!read(pair)) {
            return false;
        }
        estimator.add_sample(pair.native_ticks, pair.canonical_ns);
        return true;
    }

private:
    void* self_{nullptr};                                 ///< Borrowed backend state.
    DeviceReadPair read_{nullptr};                        ///< Pair reader.
    std::uint16_t domain_id_{0};                          ///< Published domain id.
    std::uint64_t epoch_{0};                              ///< Published epoch.
    std::string_view name_{};                             ///< Borrowed device name.
    DeviceAccessMode mode_{DeviceAccessMode::ManualPair}; ///< Access mode.
};

/// @brief Firmware register block of the D1-D3 contract.
///
/// Every field here is part of the documented contract in
/// `docs/firmware-time-base-register-map.md`; the simulated device fills all of
/// them so tests exercise the complete map.
struct DeviceRegisterMap {
    // D1 — identity/capabilities.
    std::uint32_t magic{0x4E58'5442U};   ///< `"NXTB"`.
    std::uint16_t version{1U};           ///< Register-map version.
    std::uint16_t caps{0U};              ///< Bit 0 PTM, bit 1 latch, bit 2 PHC.
    std::array<std::uint8_t, 16> uuid{}; ///< Device UUID.
    // D2 — latched correlation pair.
    std::uint64_t dev_ticks{0};     ///< Device counter at the latch edge.
    std::uint64_t ptm_ticks{0};     ///< PTM/host ticks at the same edge.
    std::uint32_t pair_sequence{0}; ///< Increments on every new pair.
    std::uint32_t pair_crc{0};      ///< CRC-32C of D1/D2 payload.
    // D3 — status.
    std::uint8_t lock_state{0};               ///< 0 free-run, 1 locked, 2 holdover.
    std::uint8_t lock_lost_sticky{0};         ///< Sticky lock-loss flag.
    std::uint16_t sync_epoch{0};              ///< Device lifecycle epoch.
    std::uint64_t last_exchange_dev_ticks{0}; ///< Counter of the last exchange.
    std::uint32_t prop_delay_ns{0};           ///< Measured propagation delay.
    std::uint32_t residual_ns{0};             ///< Last residual.
};

/// @brief Snapshot callback returning the current D1-D3 map without latching.
///
/// @param self Backend state.
/// @param out Destination register block.
/// @return `true` when a snapshot was copied.
using RegisterSnapshot = bool (*)(void*, DeviceRegisterMap&) noexcept;

/// @brief Optional latch-edge callback invoked once before a pair is read.
///
/// @param self Backend state.
using RegisterLatch = void (*)(void*) noexcept;

/// @brief D1-D3 latched-pair backend with the sequence/CRC acceptance protocol.
///
/// Reads the register block twice, accepts the pair only when `pair_sequence`
/// did not move between the reads and `pair_crc` covers the D1/D2 bytes before
/// it, and rejects a sequence that was already accepted (stale pair). Torn or
/// stale reads are discarded, never averaged.
///
/// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
class RegisterPairDevice {
public:
    /// @brief Bind the reader to its snapshot callback.
    ///
    /// @param domain_id Domain id the backend publishes.
    /// @param epoch Device lifecycle epoch.
    /// @param name Borrowed device name.
    /// @param self Opaque state passed to the callback.
    /// @param snapshot Register-block snapshot reader.
    /// @param latch Optional latch-edge callback run once per pair read; the
    ///              simulated device uses it to emulate firmware latching.
    RegisterPairDevice(std::uint16_t domain_id, std::uint64_t epoch, std::string_view name,
                       void* self, RegisterSnapshot snapshot,
                       RegisterLatch latch = nullptr) noexcept
        : domain_id_(domain_id), epoch_(epoch), name_(name), self_(self), snapshot_(snapshot),
          latch_(latch) {}

    /// @brief Read one pair under the sequence/CRC protocol.
    /// @param out Destination pair.
    /// @return `false` for a torn read, a bad CRC, a stale sequence, or an
    ///         invalid identity.
    [[nodiscard]] bool read_pair(TimeBaseCorrelationPair& out) noexcept;

    /// @brief Backend adapter over this reader.
    /// @return Callable backend that applies the sequence/CRC protocol.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] TimeBaseDeviceBackend backend() noexcept;

private:
    static bool read_thunk(void* self, TimeBaseCorrelationPair& out) noexcept;

    std::uint16_t domain_id_{0};
    std::uint64_t epoch_{1};
    std::string_view name_{};
    void* self_{nullptr};
    RegisterSnapshot snapshot_{nullptr};
    RegisterLatch latch_{nullptr};
    std::uint32_t last_sequence_{0};
    bool has_sequence_{false};
};

/// @brief In-memory device implementing the register contract.
///
/// The device advances its free-running counter on every read and returns the
/// corresponding hub instant plus a deterministic one-sided latency, so the
/// estimator sees realistic, repeatable pairs.
class SimulatedTimeBaseDevice {
public:
    /// @brief Configure the simulated device.
    ///
    /// @param domain_id Domain id the backend publishes.
    /// @param epoch Device lifecycle epoch.
    /// @param ticks_per_ns_q32 Scale: device ticks per hub ns in Q32.
    /// @param base_ticks Counter value at construction.
    /// @param latency_ns One-sided read latency added to the hub side.
    SimulatedTimeBaseDevice(std::uint16_t domain_id, std::uint64_t epoch,
                            std::uint64_t ticks_per_ns_q32, std::uint64_t base_ticks,
                            std::uint64_t latency_ns) noexcept;

    /// @brief Register block of the device (all D1-D3 fields).
    /// @return Borrowed view of the current register map.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] const DeviceRegisterMap& registers() const noexcept { return registers_; }

    /// @brief Advance the device and read one fresh latched pair.
    /// @param out Destination pair.
    /// @return Always `true`.
    [[nodiscard]] bool read_pair(TimeBaseCorrelationPair& out) noexcept;

    /// @brief Latch a new register pair without returning it.
    ///
    /// Increments `pair_sequence`, recomputes the D1/D2 CRC-32C and stamps the
    /// counter, mirroring a firmware latch edge.
    void advance_pair() noexcept;

    /// @brief Copy the current D1-D3 block without latching.
    ///
    /// The sequence/CRC protocol requires two consecutive stable reads.
    ///
    /// @param out Destination register block.
    /// @return Always `true`.
    [[nodiscard]] bool snapshot_registers(DeviceRegisterMap& out) noexcept;

    /// @brief Backend adapter that applies the D1-D3 sequence/CRC protocol.
    /// @return A callable backend for the daemon loop.
    [[nodiscard]] RegisterPairDevice register_pair_backend() noexcept;

    /// @brief Backend adapter over this device.
    /// @return A callable backend for the daemon loop.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] TimeBaseDeviceBackend backend() noexcept;

private:
    static bool read_thunk(void* self, TimeBaseCorrelationPair& out) noexcept;
    static bool snapshot_thunk(void* self, DeviceRegisterMap& out) noexcept;
    static void advance_thunk(void* self) noexcept;

    DeviceRegisterMap registers_{};
    std::uint16_t domain_id_{0};
    std::uint64_t epoch_{1};
    std::uint64_t ticks_per_ns_q32_{1ULL << 32U};
    std::uint64_t base_ticks_{0};
    std::int64_t base_canonical_ns_{0};
    std::uint64_t latency_ns_{0};
    std::uint64_t reads_{0};
};

/// @brief Simulated PTP hardware clock using cross-timestamp semantics.
///
/// Produces the same kind of precise pair a `PTP_SYS_OFFSET_PRECISE` ioctl
/// returns: the hub and device sides are captured together, with a
/// deterministic one-sided latency.
class SimulatedPhcDevice {
public:
    /// @brief Configure the simulated PHC.
    ///
    /// @param domain_id Domain id the backend publishes.
    /// @param epoch Device lifecycle epoch.
    /// @param ticks_per_ns_q32 Scale: PHC ticks per hub ns in Q32.
    /// @param base_ticks Counter value at construction.
    /// @param latency_ns One-sided cross-timestamp latency.
    SimulatedPhcDevice(std::uint16_t domain_id, std::uint64_t epoch, std::uint64_t ticks_per_ns_q32,
                       std::uint64_t base_ticks, std::uint64_t latency_ns) noexcept;

    /// @brief Read one precise cross-timestamp pair.
    /// @param out Destination pair.
    /// @return Always `true`.
    [[nodiscard]] bool read_pair(TimeBaseCorrelationPair& out) noexcept;

    /// @brief Backend adapter over this PHC.
    /// @return A callable cross-timestamp backend.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] TimeBaseDeviceBackend backend() noexcept;

private:
    static bool read_thunk(void* self, TimeBaseCorrelationPair& out) noexcept;

    std::uint16_t domain_id_{0};
    std::uint64_t epoch_{1};
    std::uint64_t ticks_per_ns_q32_{1ULL << 32U};
    std::uint64_t base_ticks_{0};
    std::int64_t base_canonical_ns_{0};
    std::uint64_t latency_ns_{0};
    std::uint64_t reads_{0};
};

/// @brief Manual pair reader for legacy modules without PCIe PTM or a latch.
///
/// Reads the device counter with `counter` and `CLOCK_MONOTONIC_RAW` with
/// `raw_clock` on both sides of the counter read. The read window is published
/// as the pair's latency and one-sided uncertainty, and the row is marked
/// `manual_pair` with `ptm_capable=false`. The driver never converts ticks.
class ManualPairDevice {
public:
    /// @brief Device-counter reader.
    using CounterRead = std::uint64_t (*)(void*) noexcept;
    /// @brief `CLOCK_MONOTONIC_RAW` reader (nanoseconds).
    using RawClockRead = std::uint64_t (*)(void*) noexcept;

    /// @brief Bind the manual reader to its callbacks.
    ///
    /// @param domain_id Domain id the backend publishes.
    /// @param epoch Device lifecycle epoch.
    /// @param name Borrowed device name.
    /// @param self Opaque state passed to the callbacks.
    /// @param counter Device counter reader.
    /// @param raw_clock Hub clock reader.
    ManualPairDevice(std::uint16_t domain_id, std::uint64_t epoch, std::string_view name,
                     void* self, CounterRead counter, RawClockRead raw_clock) noexcept
        : domain_id_(domain_id), epoch_(epoch), name_(name), self_(self), counter_(counter),
          raw_clock_(raw_clock) {}

    /// @brief Read one manual pair.
    ///
    /// @param out Destination pair; `read_latency_ns` is the measured window and
    ///            `uncertainty_ns` its half.
    /// @return `true` when both callbacks are present and the window is sane.
    [[nodiscard]] bool read_pair(TimeBaseCorrelationPair& out) noexcept;

    /// @brief Backend adapter over this reader.
    /// @return A callable manual-pair backend.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] TimeBaseDeviceBackend backend() noexcept;

private:
    static bool read_thunk(void* self, TimeBaseCorrelationPair& out) noexcept;

    std::uint16_t domain_id_{0};
    std::uint64_t epoch_{1};
    std::string_view name_{};
    void* self_{nullptr};
    CounterRead counter_{nullptr};
    RawClockRead raw_clock_{nullptr};
};

} // namespace nexigen::chrono
