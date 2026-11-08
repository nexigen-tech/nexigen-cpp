// Split from logging_test.cpp: nexigen::logging worker affinity.

#include "logging_test_support.hpp"

namespace {

void test_affinity() {
    std::uint64_t mask = 0;
    NX_CHECK(detail::self_affinity(mask).has_value());
    NX_CHECK(mask != 0ULL);

    // mask 0 is rejected without touching affinity state.
    const Status zero_res = detail::pin_self(0ULL);
    NX_CHECK(!zero_res.has_value());
    NX_CHECK(zero_res.error() == ErrorCode::InvalidArgument);

    int allowed = 0;
    for (std::uint64_t m = mask; m != 0ULL; m &= m - 1ULL) {
        ++allowed;
    }
    if (allowed >= 2) {
        const std::uint64_t target = mask & (~mask + 1ULL); // lowest permitted core
        Status pin{};
        std::uint64_t seen = 0;
        std::jthread probe([&pin, &seen, target] {
            pin = detail::pin_self(target);
            static_cast<void>(detail::self_affinity(seen));
        });
        probe.join();
        NX_CHECK(pin.has_value());
        NX_CHECK(seen == target);
        std::printf("affinity: probe pinned to single core (mask 0x%llx) OK\n",
                    static_cast<unsigned long long>(target));
    } else {
        std::printf("affinity: only 1 permitted core — pin path skipped\n");
    }
}

// --- rotation suite (task 5.5 / spec logging/file-logger rotation).

} // namespace

void suite_affinity() {
    run_suite("affinity", test_affinity);
}
