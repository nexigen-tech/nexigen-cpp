// nexigen::core — runnable SPSC ring-buffer telemetry example.
//
// One producer thread streams telemetry packets through a statically stored
// wait-free queue while one consumer thread drains it. Both endpoints observe
// the classified full/empty outcomes via has_value()/.error() (never
// value()), format ErrorCode directly with std::format, and finish with a
// cold-path queue.to_string() summary.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <thread>
#include <type_traits>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>

namespace {

/// One telemetry sample crossing the producer/consumer boundary. The layout
/// is deliberately register-sized and trivially copyable so a by-value push
/// into the queue costs a bounded copy of three words.
struct TelemetryPacket {
    nexigen::chrono::Timestamp stamp{}; ///< Monotonic capture instant (ns).
    std::uint64_t sequence{};           ///< Strictly increasing packet id.
    double sensor_value{};              ///< Synthetic sensor reading.

    /// Cold-path diagnostic (mandate 9): names every field.
    [[nodiscard]] std::string to_string() const {
        return std::format("TelemetryPacket(stamp_ns={}, sequence={}, sensor_value={:.4f})",
                           stamp.nanoseconds(), sequence, sensor_value);
    }

    /// Free form via ADL (mandate 9): mirrors the member.
    [[nodiscard]] friend std::string to_string(const TelemetryPacket& packet) {
        return packet.to_string();
    }
};

static_assert(std::is_trivially_copyable_v<TelemetryPacket>,
              "TelemetryPacket must stay trivially copyable");
static_assert(sizeof(TelemetryPacket) == 24, "TelemetryPacket must stay register-sized");

/// Total packets streamed; larger than the queue capacity so the producer
/// provably observes BufferFull and the consumer provably drains to empty.
constexpr std::uint64_t k_packet_count = 4096;

/// The queue outlives both threads (static storage: the type is non-movable).
/// Capacity 1024: producer/consumer index cells and payload each own their
/// cache lines; publication is release/acquire, the steady path is relaxed.
nexigen::core::SpscRingBuffer<TelemetryPacket, 1024> queue{};

/// Set by the producer as soon as it observes its first BufferFull, so main
/// can start the consumer only after the full outcome is demonstrated.
std::atomic<bool> producer_saw_full{false};

/// Set by the producer if the monotonic clock read fails (practically never).
std::atomic<bool> producer_failed{false};

void print_line(const std::string& line) {
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
}

} // namespace

/// One endpoint's classified outcomes.
struct EndpointStats {
    std::uint64_t retries{0};
    nexigen::core::ErrorCode last_error{nexigen::core::ErrorCode::Ok};
};

/// (1) Producer: stream every packet and retry wait-free while the queue is
/// full — a full queue is normal backpressure, never a reason to sleep.
void run_producer(EndpointStats& stats) {
    for (std::uint64_t sequence = 0; sequence < k_packet_count; ++sequence) {
        TelemetryPacket packet{};
        packet.sequence = sequence;
        packet.sensor_value = static_cast<double>(sequence) * 0.0025;

        const auto now = nexigen::chrono::Timestamp::now();
        if (!now.has_value()) { // never value(): SystemError would abort
            producer_failed.store(true, std::memory_order_release);
            stats.last_error = now.error();
            return;
        }
        packet.stamp = *now;

        for (;;) {
            const auto pushed = queue.try_push(packet);
            if (pushed.has_value()) {
                break;
            }
            ++stats.retries;
            stats.last_error = pushed.error(); // classified, never value()
            producer_saw_full.store(true, std::memory_order_release);
        }
    }
}

/// (2) Guarantee the BufferFull outcome is part of the demonstration: start the
/// consumer only once the producer has run into a full queue (bounded spin).
void await_first_full() {
    std::uint64_t spins = 0;
    constexpr std::uint64_t k_spin_budget = 100'000'000;
    while (!producer_saw_full.load(std::memory_order_acquire)) {
        if (++spins >= k_spin_budget) {
            break; // fall through: the final checks report the failure
        }
    }
}

/// (3) Consumer: drain exactly k_packet_count packets in order, then prove the
/// queue is empty with one deliberate classified pop.
void run_consumer(EndpointStats& stats, std::uint64_t& order_violations) {
    std::uint64_t expected = 0;
    while (expected < k_packet_count) {
        TelemetryPacket packet{};
        if (queue.try_pop(packet).has_value()) {
            if (packet.sequence != expected ||
                packet.sensor_value != static_cast<double>(expected) * 0.0025) {
                ++order_violations;
            }
            ++expected;
        } else {
            ++stats.retries; // BufferEmpty: wait-free, retry immediately
            stats.last_error = nexigen::core::ErrorCode::BufferEmpty;
        }
    }
    TelemetryPacket extra{};
    const auto outcome = queue.try_pop(extra);
    if (!outcome.has_value()) {
        stats.last_error = outcome.error();
    } else {
        ++order_violations; // nothing may remain after k_packet_count pops
    }
}

/// (4) Final checks; each failure names its reason on stdout.
bool report(const EndpointStats& producer, const EndpointStats& consumer,
            std::uint64_t order_violations) {
    bool ok = true;
    if (producer_failed.load(std::memory_order_acquire)) {
        print_line("producer failed to read the monotonic clock");
        ok = false;
    }
    if (producer.last_error != nexigen::core::ErrorCode::BufferFull) {
        print_line("producer never observed BufferFull");
        ok = false;
    }
    if (consumer.last_error != nexigen::core::ErrorCode::BufferEmpty) {
        print_line("consumer did not finish on a classified BufferEmpty");
        ok = false;
    }
    if (order_violations != 0) {
        print_line(std::format("order/value violations: {}", order_violations));
        ok = false;
    }
    if (!queue.is_empty()) {
        print_line("queue is not empty after the consumer finished");
        ok = false;
    }
    return ok;
}

int main() {
    // Cold-path diagnostic via the ADL free function (mandate 9).
    TelemetryPacket sample{};
    sample.sensor_value = 0.0025;
    print_line(std::format("sample packet: {}", to_string(sample)));

    EndpointStats producer_stats;
    std::jthread producer{[&] { run_producer(producer_stats); }};
    await_first_full();

    EndpointStats consumer_stats;
    std::uint64_t order_violations = 0;
    std::jthread consumer{};
    if (!producer_failed.load(std::memory_order_acquire)) {
        consumer = std::jthread{[&] { run_consumer(consumer_stats, order_violations); }};
    }

    producer.join();
    if (consumer.joinable()) {
        consumer.join();
    }

    print_line(std::format("producer: sent={}, full_retries={}, last_outcome={}", k_packet_count,
                           producer_stats.retries, producer_stats.last_error));
    print_line(std::format("consumer: received={}, empty_retries={}, last_outcome={}",
                           k_packet_count, consumer_stats.retries, consumer_stats.last_error));
    print_line(std::format("final queue state: {}", queue.to_string()));

    const bool ok = report(producer_stats, consumer_stats, order_violations);
    print_line(ok ? "ex_spsc_ring_buffer: OK" : "ex_spsc_ring_buffer: FAILED");
    return ok ? 0 : 1;
}
