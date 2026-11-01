// nexigen::runner — coverage instrumentation helper (coverage builds only).
//
// Forked supervisors end with `_exit()` and the application child replaces its
// image with `execvp()`. Both skip libgcov's exit-time dump, which makes the
// work performed in those children invisible to the coverage report. Flushing
// explicitly before those transitions keeps the measurement honest without
// changing non-coverage builds: `coverage_flush()` is then an empty inline.
#pragma once

#if defined(NEXIGEN_ENABLE_COVERAGE)
extern "C" void __gcov_dump(void);
#endif

namespace nexigen::runner::detail {

[[maybe_unused]] inline void coverage_flush() noexcept {
#if defined(NEXIGEN_ENABLE_COVERAGE)
    __gcov_dump();
#endif
}

} // namespace nexigen::runner::detail
