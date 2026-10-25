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

int main() {
    std::uint64_t producer_retries = 0;
    nexigen::core::ErrorCode producer_last_error = nexigen::core::ErrorCode::Ok;

    std::jthread producer{[&] {
        for (std::uint64_t sequence = 0; sequence < k_packet_count; ++sequence) {
            TelemetryPacket packet{};
            packet.sequence = sequence;
            packet.sensor_value = static_cast<double>(sequence) * 0.0025;

            const auto now = nexigen::chrono::Timestamp::now();
            if (!now.has_value()) { // never value(): SystemError would abort
                producer_failed.store(true, std::memory_order_release);
                producer_last_error = now.error();
                return;
            }
            packet.stamp = *now;

            // Wait-free retry: a full queue is a normal backpressure outcome,
            // never a reason to block or sleep on the producer thread.
            for (;;) {
                const auto pushed = queue.try_push(packet);
                if (pushed.has_value()) {
                    break;
                }
                ++producer_retries;
                producer_last_error = pushed.error(); // classified, never value()
                producer_saw_full.store(true, std::memory_order_release);
            }
        }
    }};

    // Guarantee the BufferFull outcome is part of the demonstration: start
    // the consumer only once the producer has run into a full queue.
    std::uint64_t spins = 0;
    constexpr std::uint64_t k_spin_budget = 100'000'000;
    while (!producer_saw_full.load(std::memory_order_acquire)) {
        if (++spins >= k_spin_budget) {
            break; // fall through: the final checks report the failure
        }
    }

    std::uint64_t consumer_retries = 0;
    std::uint64_t order_violations = 0;
    nexigen::core::ErrorCode consumer_last_error = nexigen::core::ErrorCode::Ok;

    std::jthread consumer{};
    if (!producer_failed.load(std::memory_order_acquire)) {
        consumer = std::jthread{[&] {
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
                    ++consumer_retries; // BufferEmpty: wait-free, retry immediately
                    consumer_last_error = nexigen::core::ErrorCode::BufferEmpty;
                }
            }
            // One deliberate pop past the end demonstrates the empty outcome.
            TelemetryPacket extra{};
            const auto outcome = queue.try_pop(extra);
            if (!outcome.has_value()) {
                consumer_last_error = outcome.error();
            } else {
                ++order_violations; // nothing may remain after k_packet_count pops
            }
        }};
    }

    producer.join();
    if (consumer.joinable()) {
        consumer.join();
    }

    print_line(std::format("producer: sent={}, full_retries={}, last_outcome={}",
                           k_packet_count, producer_retries, producer_last_error));
    print_line(std::format("consumer: received={}, empty_retries={}, last_outcome={}",
                           k_packet_count, consumer_retries, consumer_last_error));
    print_line(std::format("final queue state: {}", queue.to_string()));

    bool ok = true;
    if (producer_failed.load(std::memory_order_acquire)) {
        print_line("producer failed to read the monotonic clock");
        ok = false;
    }
    if (producer_last_error != nexigen::core::ErrorCode::BufferFull) {
        print_line("producer never observed BufferFull");
        ok = false;
    }
    if (consumer_last_error != nexigen::core::ErrorCode::BufferEmpty) {
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

    print_line(ok ? "ex_spsc_ring_buffer: OK" : "ex_spsc_ring_buffer: FAILED");
    return ok ? 0 : 1;
}
