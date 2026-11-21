// Tests nexigen::core diagnostics vocabulary, binding, and null consumer
// (change add-telemetry-diagnostics; spec core/diagnostics).
#include <cstdio>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <malloc.h>

#include <nlohmann/json.hpp>

#include <nexigen/core/concepts/diagnostic_logger_concept.hpp>
#include <nexigen/core/diagnostic_binding.hpp>
#include <nexigen/core/diagnostic_event.hpp>
#include <nexigen/core/diagnostic_kind.hpp>
#include <nexigen/core/diagnostic_source.hpp>
#include <nexigen/core/null_diagnostic_logger.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace core = nexigen::core;

namespace {

int g_checks = 0;
int g_failures = 0;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

void run_suite(const char* name, void (*body)()) {
    const int checks_before = g_checks;
    const int failures_before = g_failures;
    body();
    std::printf("[%-24s] %d checks, %d failed\n", name, g_checks - checks_before,
                g_failures - failures_before);
}

struct NoDiagnostic {};

static_assert(core::concepts::DiagnosticLogger<core::NullDiagnosticLogger>);
static_assert(!core::concepts::DiagnosticLogger<NoDiagnostic>);
static_assert(sizeof(core::DiagnosticEvent) == 3U);
static_assert(sizeof(core::DiagnosticBinding) == 2U * sizeof(void*));

template <class Enum, std::size_t N>
void check_enum(const Enum (&values)[N], std::string_view first_name) {
    for (std::size_t i = 0; i < N; ++i) {
        const std::string_view name = core::to_string(values[i]);
        NX_CHECK(!name.empty());
        NX_CHECK(name != "Unknown");

        Enum parsed{};
        NX_CHECK(core::from_string(name, parsed));
        NX_CHECK(parsed == values[i]);

        NX_CHECK(std::format("{}", values[i]) == name);

        nlohmann::json j;
        core::to_json(j, values[i]);
        NX_CHECK(j.is_string());
        Enum round{};
        core::from_json(j, round);
        NX_CHECK(round == values[i]);
    }
    NX_CHECK(core::to_string(values[0]) == first_name);

    Enum untouched{};
    NX_CHECK(!core::from_string("bogus-name", untouched));
    NX_CHECK(!core::from_string("Unknown", untouched));
}

void test_enum_vocabulary() {
    constexpr core::DiagnosticSource sources[] = {
        core::DiagnosticSource::Chrono,
        core::DiagnosticSource::Metrics,
        core::DiagnosticSource::Tracing,
    };
    check_enum(sources, "chrono");

    constexpr core::DiagnosticKind kinds[] = {
        core::DiagnosticKind::Started,
        core::DiagnosticKind::Degraded,
        core::DiagnosticKind::Recovered,
        core::DiagnosticKind::Quiesced,
    };
    check_enum(kinds, "started");

    // Unknown enum names leave the destination unchanged.
    core::DiagnosticSource source = core::DiagnosticSource::Metrics;
    const nlohmann::json bad_name = "nope";
    core::from_json(bad_name, source);
    NX_CHECK(source == core::DiagnosticSource::Metrics);
    const nlohmann::json not_a_string = 42;
    core::from_json(not_a_string, source);
    NX_CHECK(source == core::DiagnosticSource::Metrics);
}

void test_event_serialization() {
    const core::DiagnosticEvent event{core::DiagnosticSource::Metrics,
                                      core::DiagnosticKind::Degraded,
                                      core::ErrorCode::BufferFull};

    const std::string text = event.to_string();
    NX_CHECK(!text.empty());
    NX_CHECK(text.find("metrics") != std::string::npos);
    NX_CHECK(text.find("degraded") != std::string::npos);
    NX_CHECK(text.find("BufferFull") != std::string::npos);
    NX_CHECK(std::format("{}", event) == text);

    nlohmann::json j;
    core::to_json(j, event);
    NX_CHECK(j.is_object());
    NX_CHECK(j.at("source").get<std::string>() == "metrics");
    NX_CHECK(j.at("kind").get<std::string>() == "degraded");
    NX_CHECK(j.at("error").get<std::string>() == "BufferFull");

    core::DiagnosticEvent parsed{};
    core::from_json(j, parsed);
    NX_CHECK(parsed.source == event.source);
    NX_CHECK(parsed.kind == event.kind);
    NX_CHECK(parsed.error == event.error);

    // Non-object and unknown fields leave the event unchanged.
    core::DiagnosticEvent unchanged{core::DiagnosticSource::Tracing,
                                    core::DiagnosticKind::Started, core::ErrorCode::Ok};
    core::from_json(nlohmann::json{"not-an-object"}, unchanged);
    NX_CHECK(unchanged.source == core::DiagnosticSource::Tracing);
    core::from_json(nlohmann::json{{"source", "bogus"}, {"kind", 5}, {"error", "Nope"}}, unchanged);
    NX_CHECK(unchanged.source == core::DiagnosticSource::Tracing);
    NX_CHECK(unchanged.kind == core::DiagnosticKind::Started);
    NX_CHECK(unchanged.error == core::ErrorCode::Ok);
}

struct Collector {
    int count{0};
    core::DiagnosticEvent last{};
};

void test_binding_dispatch() {
    Collector collector{};
    const core::DiagnosticEvent event{core::DiagnosticSource::Chrono,
                                      core::DiagnosticKind::Started, core::ErrorCode::Ok};
    const core::DiagnosticBinding binding{
        &collector,
        [](void* self, const core::DiagnosticEvent& e) noexcept {
            auto* c = static_cast<Collector*>(self);
            ++c->count;
            c->last = e;
        }};

    NX_CHECK(binding.to_string() == "DiagnosticBinding(bound=true)");
    binding.on_event(binding.self, event);
    NX_CHECK(collector.count == 1);
    NX_CHECK(collector.last.source == event.source);
    NX_CHECK(collector.last.kind == event.kind);

    // Emitting through a bound callback does not allocate.
    const std::size_t heap_before = mallinfo2().uordblks;
    for (int i = 0; i < 1000; ++i) {
        binding.on_event(binding.self, event);
    }
    NX_CHECK(mallinfo2().uordblks == heap_before);
    NX_CHECK(collector.count == 1001);

    // A default binding is a successful no-op.
    const core::DiagnosticBinding unbound{};
    NX_CHECK(unbound.on_event == nullptr);
    NX_CHECK(unbound.to_string() == "DiagnosticBinding(bound=false)");
}

void test_null_logger() {
    core::NullDiagnosticLogger logger{};
    const core::DiagnosticEvent event{core::DiagnosticSource::Tracing,
                                      core::DiagnosticKind::Quiesced, core::ErrorCode::Ok};
    logger.diagnostic(event);
    NX_CHECK(logger.to_string() == "NullDiagnosticLogger");
    NX_CHECK(std::format("{}", logger) == "NullDiagnosticLogger");

    // The concept is usable as a compile-time gate.
    auto accept = [](core::concepts::DiagnosticLogger auto& consumer,
                     const core::DiagnosticEvent& e) noexcept { consumer.diagnostic(e); };
    accept(logger, event);
    NX_CHECK(true);
}

} // namespace

int main() {
    run_suite("enum_vocabulary", test_enum_vocabulary);
    run_suite("event_serialization", test_event_serialization);
    run_suite("binding_dispatch", test_binding_dispatch);
    run_suite("null_logger", test_null_logger);
    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
