// Split from logging_test.cpp: nexigen::logging log-level concepts and ordering.

#include "logging_test_support.hpp"

namespace {

// --- concept fixtures (task 2.2 / design D3): one complete surface plus one
// deliberately-broken variant per required member. Each negative proves the
// concept *rejects* the defect — absence/signature errors are compile law.

// --- concept fixtures (change add-serialization-and-wait-free-logging, task
// 6.1): one complete variadic surface plus one deliberately-broken variant
// per required member/shape. Each negative proves the concept *rejects* the
// defect — absence/signature errors are compile law.

// The five conveniences below `fatal` are boilerplate in every fixture.
#define NX_CONV_5()                                                                                \
    template <class... A> void verbose(detail::format_param_t<A...> f, A&&... a) noexcept {        \
        log(LogLevel::Verbose, f, std::forward<A>(a)...);                                          \
    }                                                                                              \
    template <class... A> void debug(detail::format_param_t<A...> f, A&&... a) noexcept {          \
        log(LogLevel::Debug, f, std::forward<A>(a)...);                                            \
    }                                                                                              \
    template <class... A> void info(detail::format_param_t<A...> f, A&&... a) noexcept {           \
        log(LogLevel::Info, f, std::forward<A>(a)...);                                             \
    }                                                                                              \
    template <class... A> void warning(detail::format_param_t<A...> f, A&&... a) noexcept {        \
        log(LogLevel::Warning, f, std::forward<A>(a)...);                                          \
    }                                                                                              \
    template <class... A> void error(detail::format_param_t<A...> f, A&&... a) noexcept {          \
        log(LogLevel::Error, f, std::forward<A>(a)...);                                            \
    }

/// Complete conforming surface: the positive reference for the concept.
struct FullLoggerModel {
    detail::LevelControl levels_{};
    LogLevel last_level{LogLevel::Verbose};
    int calls{0};

    void set_level(LogLevel lv) noexcept { levels_.set_level(lv); }
    [[nodiscard]] LogLevel level() const noexcept { return levels_.level(); }
    template <class... A> void log(LogLevel lv, detail::format_param_t<A...>, A&&...) noexcept {
        last_level = lv;
        ++calls;
    }
    NX_CONV_5()
    template <class... A> void fatal(detail::format_param_t<A...> f, A&&... a) noexcept {
        log(LogLevel::Fatal, f, std::forward<A>(a)...);
    }
    [[nodiscard]] Status flush() noexcept { return {}; }
};
static_assert(concepts::Logger<FullLoggerModel>);

/// Failing member for the composite flush parity test (analysis #21 /
/// change fix-p2-observability-and-writers): carries its own error code so
/// the composite's FIRST-error selection is observable.
struct FailFlushLogger {
    core::ErrorCode code{core::ErrorCode::InvalidArgument};
    void set_level(LogLevel) noexcept {}
    [[nodiscard]] LogLevel level() const noexcept { return LogLevel::Info; }
    template <class... A> void log(LogLevel, detail::format_param_t<A...>, A&&...) noexcept {}
    NX_CONV_5()
    template <class... A> void fatal(detail::format_param_t<A...> f, A&&... a) noexcept {
        log(LogLevel::Fatal, f, std::forward<A>(a)...);
    }
    [[nodiscard]] Status flush() noexcept { return std::unexpected(code); }
};

struct NoFatalModel { // fatal omitted entirely
    void set_level(LogLevel) noexcept {}
    [[nodiscard]] LogLevel level() const noexcept { return LogLevel::Info; }
    template <class... A> void log(LogLevel, detail::format_param_t<A...>, A&&...) noexcept {}
    NX_CONV_5()
    [[nodiscard]] Status flush() noexcept { return {}; }
};
static_assert(!concepts::Logger<NoFatalModel>);

struct NoFlushModel { // flush omitted entirely
    void set_level(LogLevel) noexcept {}
    [[nodiscard]] LogLevel level() const noexcept { return LogLevel::Info; }
    template <class... A> void log(LogLevel, detail::format_param_t<A...>, A&&...) noexcept {}
    NX_CONV_5()
    template <class... A> void fatal(detail::format_param_t<A...> f, A&&... a) noexcept {
        log(LogLevel::Fatal, f, std::forward<A>(a)...);
    }
};
static_assert(!concepts::Logger<NoFlushModel>);

struct LevelNonconstModel { // level() loses const
    void set_level(LogLevel) noexcept {}
    [[nodiscard]] LogLevel level() noexcept { return LogLevel::Info; }
    template <class... A> void log(LogLevel, detail::format_param_t<A...>, A&&...) noexcept {}
    NX_CONV_5()
    template <class... A> void fatal(detail::format_param_t<A...> f, A&&... a) noexcept {
        log(LogLevel::Fatal, f, std::forward<A>(a)...);
    }
    [[nodiscard]] Status flush() noexcept { return {}; }
};
static_assert(!concepts::Logger<LevelNonconstModel>);

struct LogNotNoexceptModel { // log() loses noexcept
    void set_level(LogLevel) noexcept {}
    [[nodiscard]] LogLevel level() const noexcept { return LogLevel::Info; }
    template <class... A> void log(LogLevel, detail::format_param_t<A...>, A&&...) { ++calls; }
    int calls{0};
    NX_CONV_5()
    template <class... A> void fatal(detail::format_param_t<A...>, A&&...) noexcept {}
    [[nodiscard]] Status flush() noexcept { return {}; }
};
static_assert(!concepts::Logger<LogNotNoexceptModel>);

struct LogReturnsStatusModel { // old surface: log() returns Status, not void
    void set_level(LogLevel) noexcept {}
    [[nodiscard]] LogLevel level() const noexcept { return LogLevel::Info; }
    [[nodiscard]] Status log(LogLevel, FormatWithLocation<>) noexcept { return {}; }
    template <class... A> void verbose(detail::format_param_t<A...>, A&&...) noexcept {}
    template <class... A> void debug(detail::format_param_t<A...>, A&&...) noexcept {}
    template <class... A> void info(detail::format_param_t<A...>, A&&...) noexcept {}
    template <class... A> void warning(detail::format_param_t<A...>, A&&...) noexcept {}
    template <class... A> void error(detail::format_param_t<A...>, A&&...) noexcept {}
    template <class... A> void fatal(detail::format_param_t<A...>, A&&...) noexcept {}
    [[nodiscard]] Status flush() noexcept { return {}; }
};
static_assert(!concepts::Logger<LogReturnsStatusModel>);

struct InfoNeedsLocationModel { // info() demands an explicit location arg
    void set_level(LogLevel) noexcept {}
    [[nodiscard]] LogLevel level() const noexcept { return LogLevel::Info; }
    template <class... A> void log(LogLevel, detail::format_param_t<A...>, A&&...) noexcept {}
    template <class... A> void verbose(detail::format_param_t<A...>, A&&...) noexcept {}
    template <class... A> void debug(detail::format_param_t<A...>, A&&...) noexcept {}
    template <class... A> void warning(detail::format_param_t<A...>, A&&...) noexcept {}
    template <class... A> void error(detail::format_param_t<A...>, A&&...) noexcept {}
    template <class... A>
    void info(detail::format_param_t<A...> f, std::source_location, A&&... a) noexcept {
        log(LogLevel::Info, f, std::forward<A>(a)...);
    }
    template <class... A> void fatal(detail::format_param_t<A...>, A&&...) noexcept {}
    [[nodiscard]] Status flush() noexcept { return {}; }
};
static_assert(!concepts::Logger<InfoNeedsLocationModel>);

#undef NX_CONV_5

// --- levels suite (task 2.3 / spec logging/log-level, logging/logger-concept).
void test_levels() {
    detail::LevelControl ctl{};
    NX_CHECK(ctl.level() == LogLevel::Info); // declared default threshold
    ctl.set_level(LogLevel::Warning);
    NX_CHECK(ctl.level() == LogLevel::Warning);
    ctl.set_level(LogLevel::Off);
    NX_CHECK(ctl.level() == LogLevel::Off);
    detail::LevelControl tuned{LogLevel::Debug}; // explicit initial level
    NX_CHECK(tuned.level() == LogLevel::Debug);

    // Runtime naming mirrors the constexpr table (pinned above at compile time).
    NX_CHECK(to_string(LogLevel::Warning) == "warning");
    NX_CHECK(to_string(LogLevel::Off) == "off");

    // Positive fixture exercises the concept surface, incl. literal convenience
    // calls with implicit caller-site capture (spec logger-concept).
    FullLoggerModel model{};
    model.info("hello");
    NX_CHECK(model.last_level == LogLevel::Info);
    NX_CHECK(model.calls == 1);
    model.debug("value {}", 42); // argument pack shape
    NX_CHECK(model.last_level == LogLevel::Debug);
    NX_CHECK(model.calls == 2);
    model.fatal("boom");
    NX_CHECK(model.last_level == LogLevel::Fatal);
    NX_CHECK(model.flush().has_value());
    NX_CHECK(model.level() == LogLevel::Info);
    model.set_level(LogLevel::Error);
    NX_CHECK(model.level() == LogLevel::Error);

    // Relaxed runtime-control smoke (spec: "Reads and writes need no
    // synchronization primitives"): setter alternates two thresholds, reader
    // must only ever observe one of them. TSan must call this race-free.
    // Initial level is one of the two alternates so the reader can never see
    // the untouched default (Info) before the setter's first store.
    detail::LevelControl live{LogLevel::Debug};
    int bad = 0;
    std::jthread setter([&live] {
        for (int i = 0; i < 200'000; ++i) {
            live.set_level((i & 1) == 0 ? LogLevel::Debug : LogLevel::Off);
        }
    });
    std::jthread reader([&live, &bad] {
        for (int i = 0; i < 200'000; ++i) {
            const LogLevel seen = live.level();
            if (seen != LogLevel::Debug && seen != LogLevel::Off) {
                ++bad;
            }
        }
    });
    NX_CHECK(bad == 0); // jthread destructors join both threads
    std::printf("levels: relaxed control smoke clean (bad=%d)\n", bad);

    // Composite flush parity (analysis #21): the FIRST member error wins,
    // every member still flushed. Members: OK, InvalidArgument (first),
    // SystemError (second, suppressed in the return) — configured through
    // the by-index accessors (spec logging/composite-logger).
    {
        CompositeLogger<NullLogger, FailFlushLogger, FailFlushLogger> composite{};
        composite.get<1>().code = core::ErrorCode::InvalidArgument;
        composite.get<2>().code = core::ErrorCode::SystemError;
        const core::Status flushed = composite.flush();
        NX_CHECK(!flushed.has_value());
        NX_CHECK(flushed.error() == core::ErrorCode::InvalidArgument);
    }
}

} // namespace

void suite_levels() {
    run_suite("levels", test_levels);
}
