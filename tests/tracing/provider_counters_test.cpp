// nexigen::tracing — provider-owned meter counters.
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <thread>

#include <nexigen/core/concepts/meter_concept.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/tracing/null_trace_sink.hpp>
#include <nexigen/tracing/tracer_provider.hpp>
#include <nexigen/tracing/tracer_provider_config.hpp>

#include "tracing_test_support.hpp"

namespace {
struct FakeMeter {
    struct Counter {
        std::uint64_t value{0};
        void add(std::uint64_t delta) noexcept { value += delta; }
        void inc() noexcept { ++value; }
    };
    Counter started{};
    Counter buffer_full{};
    Counter slot_exhausted{};

    template <class... Policies>
    [[nodiscard]] nexigen::core::Result<Counter*> create_counter(std::string_view name,
                                                                 std::string_view = {}) noexcept {
        if (name == "started") return &started;
        if (name == "dropped.buffer_full") return &buffer_full;
        if (name == "dropped.slot_exhausted") return &slot_exhausted;
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
};
static_assert(nexigen::core::concepts::MeterLike<FakeMeter>);
} // namespace

void test_provider_counters() {
    FakeMeter meter{};
    nexigen::tracing::NullTraceSink sink{};
    nexigen::tracing::TracerProviderConfig config{};
    nexigen::tracing::TracerProvider<64, 4> provider{config, sink, &meter};
    NX_CHECK(provider.initialize().has_value());
    auto tracer = provider.get_tracer("counter-test");
    NX_CHECK(tracer.has_value());
    if (!tracer.has_value()) {
        return;
    }
    {
        auto span = (*tracer)->start_span("span", nexigen::tracing::SpanKind::Internal);
        static_cast<void>(span);
    }
    NX_CHECK(provider.flush().has_value());
    NX_CHECK(meter.started.value == 1U);
    NX_CHECK(meter.buffer_full.value == 0U);
    NX_CHECK(meter.slot_exhausted.value == 0U);

    FakeMeter slot_meter{};
    nexigen::tracing::TracerProvider<64, 1> single{config, sink, &slot_meter};
    NX_CHECK(single.initialize().has_value());
    auto single_tracer = single.get_tracer("slot-test");
    NX_CHECK(single_tracer.has_value());
    if (!single_tracer.has_value()) {
        return;
    }
    auto held = (*single_tracer)->start_span("held", nexigen::tracing::SpanKind::Internal);
    std::thread other([&single_tracer] {
        auto span = (*single_tracer)->start_span("other", nexigen::tracing::SpanKind::Internal);
        static_cast<void>(span);
    });
    other.join();
    NX_CHECK(slot_meter.slot_exhausted.value == 1U);
    static_cast<void>(held);
}
