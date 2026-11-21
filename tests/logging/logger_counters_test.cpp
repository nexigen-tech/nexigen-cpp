// nexigen::logging — meter counters on the producer path.
#include <cstdio>
#include <string>
#include <string_view>

#include "logging_test_support.hpp"

#include <nexigen/core/concepts/meter_concept.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/detail/producer.hpp>

namespace {

struct FakeMeter {
    struct Counter {
        std::uint64_t value{0};
        void add(std::uint64_t delta) noexcept { value += delta; }
        void inc() noexcept { ++value; }
    };

    Counter accepted{};
    Counter rate_limited{};
    Counter buffer_full{};
    Counter slot_exhausted{};

    template <class... Policies>
    [[nodiscard]] nexigen::core::Result<Counter*> create_counter(std::string_view name,
                                                                 std::string_view = {}) noexcept {
        if (name == "accepted") return &accepted;
        if (name == "dropped.rate_limited") return &rate_limited;
        if (name == "dropped.buffer_full") return &buffer_full;
        if (name == "dropped.slot_exhausted") return &slot_exhausted;
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
};

static_assert(nexigen::core::concepts::MeterLike<FakeMeter>);

} // namespace

void test_logger_counters_body() {
    using nexigen::core::LogLevel;
    using Producer = nexigen::logging::detail::Producer<1, 128, 1, FakeMeter>;

    FakeMeter meter{};
    Producer producer{};
    producer.bind_meter(&meter);
    NX_CHECK(producer.initialize().has_value());
    producer.set_level(LogLevel::Info);

    producer.emit(LogLevel::Info, "first");
    producer.emit(LogLevel::Info, "second");

    const auto dropped = producer.dropped();
    NX_CHECK(dropped.buffer_full == 1U);
    NX_CHECK(meter.accepted.value == 1U);
    NX_CHECK(meter.buffer_full.value == 1U);
    NX_CHECK(meter.buffer_full.value == dropped.buffer_full);

}

void suite_logger_counters() { run_suite("logger_counters", test_logger_counters_body); }
