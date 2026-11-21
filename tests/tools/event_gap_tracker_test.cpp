// nexigen-recd event gap tracker tests.
#include <cstdio>

#include "nexigen-recd/event_gap_tracker.hpp"

namespace {
int g_failures = 0;
#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #__VA_ARGS__);                      \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)
} // namespace

int main() {
    nexigen::recorder::EventGapTracker tracker{};

    CHECK(!tracker.observe(7U, 1U).has_value()); // first sample is a baseline
    const auto gap = tracker.observe(7U, 4U);
    CHECK(gap.has_value());
    CHECK(gap->first_lost_sequence == 2U);
    CHECK(gap->lost_count == 2U);
    CHECK(!tracker.observe(7U, 5U).has_value());
    CHECK(!tracker.observe(7U, 5U).has_value()); // duplicate is not a gap

    CHECK(!tracker.observe(8U, 0U).has_value()); // new epoch is not a gap
    const auto epoch_gap = tracker.observe(8U, 3U);
    CHECK(epoch_gap.has_value());
    CHECK(epoch_gap->first_lost_sequence == 1U);
    CHECK(epoch_gap->lost_count == 2U);

    std::printf("event gap tracker: %d failures\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
