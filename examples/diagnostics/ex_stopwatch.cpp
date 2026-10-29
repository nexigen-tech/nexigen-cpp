// nexigen::diagnostics runnable example: the diagnostic stopwatch (spec
// diagnostics/examples "ex_stopwatch demonstrates real-time loop budget
// checking with has_exceeded_budget" / "ex_stopwatch demonstrates multi-stage
// lap profiling").
//
// Four demonstrations, in order:
//   1. Per-tick budget checking: a simulated real-time loop where every tick
//      runs staged work inside a ScopedStopwatch guard, checks
//      has_exceeded_budget() mid-scope, and reports elapsed vs budget — with
//      an end-of-run summary of the ticks over budget.
//   2. Multi-stage lap profiling: one instrumented loop lapping at every
//      stage boundary; each stage prints its lap duration with lap_count(),
//      last_lap_ns() and the running min/max endpoints, ending with the
//      total elapsed vs the sum of stage laps.
//   3. A restart() round: a measured interval closes into a fresh one —
//      the returned total reflects the elapsed before restart, the next
//      round's readings start fresh from (near) zero.
//   4. An out-argument guard: a ScopedStopwatch writing a scope's elapsed
//      into a std::uint64_t& the caller prints afterwards.
//
// Self-verification: every demonstration checks its own invariants (budget
// labels match the checks, total covers the stage laps, restart brackets the
// prior reading, out-param is positive). A broken check prints an error on
// stderr and exits NON-ZERO; success exits 0.
//
// Self-contained: creates its own work, budgets and timing data; only the
// public diagnostics API. Run: ./ex_stopwatch
#include <cstdint>
#include <cstdio>

#include <nexigen/diagnostics/scoped_stopwatch.hpp>
#include <nexigen/diagnostics/stopwatch.hpp>

namespace {

// Deterministic staged work: roughly one "unit" per 600 volatile-add
// iterations, impossible for the optimizer to remove. Units are tuned so
// light/heavy stages are visibly different on a Debug build as well.
[[nodiscard]] std::uint64_t work_units(int units) noexcept {
    volatile std::uint64_t sink = 0;
    for (int i = 0; i < units * 600; ++i) { sink += static_cast<std::uint64_t>(i); }
    return sink;
}

bool g_ok = true;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "ex_stopwatch: FAILED check: %s\n", what);
        g_ok = false;
    }
}

} // namespace

int main()
{
    using nexigen::diagnostics::ScopedStopwatch;
    using nexigen::diagnostics::Stopwatch;

    // --- 1. Per-tick budget checking (spec "real-time loop budget checking")

    std::printf("== per-tick budget checks (ScopedStopwatch + has_exceeded_budget) ==\n");
    // Calibrate the budget between the light and heavy tick payloads so the
    // over/under split is meaningful in every build mode (Debug, Release and
    // the sanitizer builds all spin at different rates).
    std::uint64_t light_ns = 0;
    {
        ScopedStopwatch probe{light_ns};
        static_cast<void>(work_units(200));
    }
    std::uint64_t heavy_ns = 0;
    {
        ScopedStopwatch probe{heavy_ns};
        static_cast<void>(work_units(2'500));
    }
    const std::uint64_t budget_ns = light_ns + (heavy_ns - light_ns) / 2;
    std::printf("  calibrated budget: %llu ns (light payload %llu ns, heavy payload %llu ns)\n",
                static_cast<unsigned long long>(budget_ns),
                static_cast<unsigned long long>(light_ns),
                static_cast<unsigned long long>(heavy_ns));
    expect(heavy_ns > light_ns, "the heavy payload takes longer than the light one");

    constexpr int tick_count = 8;
    int ticks_over_budget = 0;
    for (int tick = 0; tick < tick_count; ++tick) {
        // Staged work per tick: even ticks light, odd ticks heavy — the
        // stage mix is what a real loop's varying payload looks like.
        const int units = (tick % 2 == 0) ? 200 : 2'500;
        std::uint64_t tick_ns = 0;
        bool over = false;
        {
            ScopedStopwatch guard{tick_ns, budget_ns};
            static_cast<void>(work_units(units));
            over = guard.has_exceeded_budget(); // mid-scope, delivers nothing
        } // guard reports tick_ns exactly once, here
        if (over) { ++ticks_over_budget; }
        std::printf("  tick %d: %8llu ns  budget %6llu ns  %s\n", tick,
                    static_cast<unsigned long long>(tick_ns),
                    static_cast<unsigned long long>(budget_ns),
                    over ? "OVER BUDGET" : "within budget");
        expect(tick_ns > 0, "tick elapsed is positive");
        expect(over == (tick_ns > budget_ns),
               "the has_exceeded_budget label matches the delivered elapsed");
    }
    std::printf("  summary: %d of %d ticks over budget\n\n", ticks_over_budget,
                tick_count);
    expect(ticks_over_budget > 0, "the heavy ticks exceed the budget");
    expect(ticks_over_budget < tick_count, "the light ticks stay within budget");

    // --- 2. Multi-stage lap profiling (spec "multi-stage lap profiling")

    std::printf("== multi-stage lap profiling (lap() at every stage boundary) ==\n");
    constexpr int stage_units[4] = {400, 1'200, 800, 1'600};
    constexpr int stage_count = 4;
    Stopwatch profile{};
    profile.start();
    std::uint64_t lap_sum = 0;
    for (int stage = 0; stage < stage_count; ++stage) {
        static_cast<void>(work_units(stage_units[stage]));
        const std::uint64_t stage_ns = profile.lap(); // split, keeps running
        lap_sum += stage_ns;
        std::printf("  stage %d: %8llu ns  (lap_count=%u, last_lap_ns=%llu, "
                    "min=%llu, max=%llu)\n",
                    stage, static_cast<unsigned long long>(stage_ns),
                    profile.lap_count(),
                    static_cast<unsigned long long>(profile.last_lap_ns()),
                    static_cast<unsigned long long>(profile.min_lap_ns()),
                    static_cast<unsigned long long>(profile.max_lap_ns()));
    }
    const std::uint64_t total_ns = profile.elapsed_ns();
    profile.stop();
    std::printf("  total: %llu ns across %u laps, sum of stage laps: %llu ns\n",
                static_cast<unsigned long long>(total_ns), profile.lap_count(),
                static_cast<unsigned long long>(lap_sum));
    std::printf("  lap endpoints: min=%llu ns, max=%llu ns\n\n",
                static_cast<unsigned long long>(profile.min_lap_ns()),
                static_cast<unsigned long long>(profile.max_lap_ns()));
    // The total elapsed matches the sum of stage laps (spec scenario,
    // "rounding into tolerance" — the tail after the last lap plus sampling).
    expect(total_ns >= lap_sum, "total covers every stage lap");
    expect(total_ns - lap_sum <= 5'000'000ULL,
           "total matches the sum of stage laps within tolerance");
    expect(profile.lap_count() == static_cast<std::uint32_t>(stage_count),
           "one lap per stage boundary");
    expect(profile.min_lap_ns() <= profile.max_lap_ns(),
           "min/max lap endpoints bracket the stage laps");

    // --- 3. restart() round (spec "Restart replaces a measured interval cleanly")

    std::printf("== restart() round (close a measured interval into a fresh one) ==\n");
    Stopwatch round{};
    round.start();
    static_cast<void>(work_units(1'000));
    const std::uint64_t before_restart = round.elapsed_ns();
    const std::uint64_t returned_total = round.restart();
    std::printf("  first round: elapsed before restart %llu ns, restart() returned %llu ns\n",
                static_cast<unsigned long long>(before_restart),
                static_cast<unsigned long long>(returned_total));
    // The printed returned total reflects the elapsed before restart.
    expect(returned_total >= before_restart, "restart() reports the elapsed before restart");
    expect(returned_total <= before_restart + 5'000'000ULL,
           "restart() reports no more than the prior reading plus sampling overhead");
    // The following round's readings start fresh from (near) zero.
    const std::uint64_t fresh = round.elapsed_ns();
    std::printf("  second round starts fresh: elapsed now %llu ns\n",
                static_cast<unsigned long long>(fresh));
    expect(fresh < returned_total, "the fresh round starts from near zero");
    static_cast<void>(work_units(500));
    const std::uint64_t second_total = round.restart();
    std::printf("  second round: restart() returned %llu ns (fresh accumulation only)\n\n",
                static_cast<unsigned long long>(second_total));
    expect(second_total > 0, "the fresh round re-accumulated from zero");
    expect(second_total <= before_restart, "the fresh round holds only its own time");
    round.reset();

    // --- 4. Out-argument guard (spec "the ScopedStopwatch out-argument
    // destination writing a scope's elapsed into a std::uint64_t&")

    std::printf("== out-argument guard (ScopedStopwatch -> std::uint64_t&) ==\n");
    std::uint64_t scope_ns = 0;
    {
        ScopedStopwatch guard{scope_ns}; // writes the scope's elapsed on exit
        static_cast<void>(work_units(600));
    }
    std::printf("  guarded scope reported %llu ns into the caller's std::uint64_t&\n",
                static_cast<unsigned long long>(scope_ns));
    expect(scope_ns > 0, "the out-argument guard delivered a positive elapsed");

    if (!g_ok) {
        std::fprintf(stderr, "ex_stopwatch: one or more checks FAILED\n");
        return 1;
    }
    std::printf("\nex_stopwatch: all checks passed\n");
    return 0;
}
