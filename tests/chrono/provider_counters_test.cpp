// nexigen::chrono — provider-owned meter counters.
#include <cstdint>
#include <cstdio>
#include <string_view>

#include <nexigen/chrono/null_sink.hpp>
#include <nexigen/chrono/provider.hpp>
#include <nexigen/chrono/provider_config.hpp>
#include <nexigen/core/concepts/meter_concept.hpp>
#include <nexigen/core/result.hpp>

namespace {
int g_failures = 0;
#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #__VA_ARGS__);                      \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

struct FakeMeter {
    struct Counter {
        std::uint64_t value{0};
        void add(std::uint64_t delta) noexcept { value += delta; }
        void inc() noexcept { ++value; }
    };
    Counter calibration_failed{};
    Counter acquire_exhausted{};
    Counter snapshot_failed{};

    template <class... Policies>
    [[nodiscard]] nexigen::core::Result<Counter*> create_counter(std::string_view name,
                                                                 std::string_view = {}) noexcept {
        if (name == "calibration_failed") return &calibration_failed;
        if (name == "acquire_exhausted") return &acquire_exhausted;
        if (name == "snapshot_failed") return &snapshot_failed;
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
};
static_assert(nexigen::core::concepts::MeterLike<FakeMeter>);
struct FailingSink {
    [[nodiscard]] nexigen::core::Status on_event(const nexigen::chrono::Snapshot&) noexcept {
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    [[nodiscard]] nexigen::core::Status flush() noexcept { return {}; }
};

} // namespace

int main() {
    using namespace nexigen::chrono;
    FakeMeter meter{};
    NullSink sink{};
    ProviderConfig config{};
    Provider<1, FakeMeter> provider{config, sink, &meter};
    CHECK(provider.prepare().has_value());

    auto first = provider.acquire();
    CHECK(first.has_value());
    auto second = provider.acquire();
    CHECK(!second.has_value());
    CHECK(meter.acquire_exhausted.value == 1U);

    FakeMeter snapshot_meter{};
    FailingSink failing{};
    Provider<1, FakeMeter> snapshot_provider{config, failing, &snapshot_meter};
    CHECK(!snapshot_provider.snapshot_now().has_value());
    CHECK(snapshot_meter.snapshot_failed.value == 1U);

    std::printf("chrono provider counters: %d failures\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
