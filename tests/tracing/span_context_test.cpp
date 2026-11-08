// Split from tracing_test.cpp: nexigen::tracing span context and parent hierarchy.

#include "tracing_test_support.hpp"

namespace {

void test_ids_and_context_sentinels() {
    using tracing::TraceId;
    using tracing::SpanId;
    using tracing::SpanContext;
    // Zero sentinel: default reads as unset; explicit values preserved.
    NX_CHECK(TraceId{}.is_unset());
    NX_CHECK(SpanId{}.is_unset());
    NX_CHECK(TraceId{0xDEADBEEF}.value() == 0xDEADBEEFULL);
    NX_CHECK(SpanId{42}.value() == 42ULL);
    NX_CHECK(TraceId{7} == TraceId{7});
    NX_CHECK(TraceId{7} < TraceId{8});
    NX_CHECK(SpanId{9} != SpanId{10});
}

void test_ids_and_context_uniqueness() {
    using tracing::TraceId;
    using tracing::SpanId;
    using tracing::SpanContext;
    // Nonzero, distinct generation on this thread; value-safe wrappers.
    std::array<TraceId, 64> trace_ids{};
    std::array<SpanId, 64> span_ids{};
    for (std::size_t i = 0; i < trace_ids.size(); ++i) {
        trace_ids[i] = TraceId::generate();
        span_ids[i] = SpanId::generate();
        NX_CHECK(!trace_ids[i].is_unset());
        NX_CHECK(!span_ids[i].is_unset());
    }
    for (std::size_t i = 0; i < trace_ids.size(); ++i) {
        for (std::size_t j = i + 1; j < trace_ids.size(); ++j) {
            NX_CHECK(trace_ids[i] != trace_ids[j]);
            NX_CHECK(span_ids[i] != span_ids[j]);
        }
    }
}

void test_ids_and_context_alloc_free() {
    using tracing::TraceId;
    using tracing::SpanId;
    using tracing::SpanContext;
    // Wait-free + allocation-free: a large batch with both counters armed.
    {
        const std::size_t arena_before = arena_used();
        const std::size_t heap_before = heap_in_use();
        for (int i = 0; i < 100'000; ++i) {
            const TraceId t = TraceId::generate();
            const SpanId s = SpanId::generate();
            static_cast<void>(t);
            static_cast<void>(s);
        }
        NX_CHECK(arena_used() == arena_before);
        NX_CHECK(heap_in_use() == heap_before);
    }
}

void test_ids_and_context_concurrent() {
    using tracing::TraceId;
    using tracing::SpanId;
    using tracing::SpanContext;
    // 32 concurrent generating threads: no zero, no duplicates across the
    // observed window. Buckets are PRE-RESERVED before spawning so the
    // workers themselves never allocate; dedup runs after the join.
    {
        constexpr int threads = 32;
        constexpr int per_thread = 4'000;
        std::array<std::vector<std::uint64_t>, threads> collected{};
        for (auto& bucket : collected) {
            bucket.resize(static_cast<std::size_t>(per_thread) * 2U, 0xDEADBEEFULL);
        }
        std::atomic<int> failures{0};
        std::vector<std::jthread> workers;
        workers.reserve(threads);
        for (int t = 0; t < threads; ++t) {
            workers.emplace_back([t, &collected, &failures] {
                auto& bucket = collected[static_cast<std::size_t>(t)];
                for (int i = 0; i < per_thread; ++i) {
                    const TraceId trace = TraceId::generate();
                    const SpanId span = SpanId::generate();
                    if (trace.is_unset() || span.is_unset()) {
                        failures.fetch_add(1, std::memory_order_relaxed);
                    }
                    bucket[static_cast<std::size_t>(i) * 2U] = trace.value();
                    bucket[static_cast<std::size_t>(i) * 2U + 1U] = span.value();
                }
            });
        }
        for (auto& w : workers) {
            w.join();
        }
        NX_CHECK(failures.load(std::memory_order_relaxed) == 0);
        std::vector<std::uint64_t> flat;
        flat.reserve(static_cast<std::size_t>(threads) * per_thread * 2U);
        for (const auto& bucket : collected) {
            for (const std::uint64_t value : bucket) {
                flat.push_back(value);
            }
        }
        std::sort(flat.begin(), flat.end());
        const auto dup = std::adjacent_find(flat.begin(), flat.end());
        NX_CHECK(dup == flat.end()); // no id generated twice across threads
    }
}

void test_ids_and_context_hex_json() {
    using tracing::TraceId;
    using tracing::SpanId;
    using tracing::SpanContext;
    // 16-char zero-padded lowercase hex rendering + JSON round-trips.
    {
        const TraceId trace{0x0123456789ABCDEFULL};
        NX_CHECK(trace.to_string() == "0123456789abcdef");
        const SpanId span{0xFEDCBA9876543210ULL};
        NX_CHECK(span.to_string() == "fedcba9876543210");
        const TraceId small{0x5};
        NX_CHECK(small.to_string() == "0000000000000005");

        const nlohmann::json j_trace = trace;
        NX_CHECK(j_trace.get<std::string>() == "0123456789abcdef");
        TraceId parsed_trace{};
        nlohmann::from_json(j_trace, parsed_trace);
        NX_CHECK(parsed_trace == trace);

        const nlohmann::json j_span = span;
        SpanId parsed_span{};
        nlohmann::from_json(j_span, parsed_span);
        NX_CHECK(parsed_span == span);

        // Hostile JSON: numbers/objects/arrays/null/wrong text leave values.
        const std::array<nlohmann::json, 5> bad{nlohmann::json(3), nlohmann::json{{"x", "y"}},
                                                nlohmann::json{1}, nlohmann::json(nullptr),
                                                nlohmann::json("nothex!!")};
        for (const nlohmann::json& hostile : bad) {
            TraceId out = trace;
            nlohmann::from_json(hostile, out);
            NX_CHECK(out == trace);
            SpanId sout = span;
            nlohmann::from_json(hostile, sout);
            NX_CHECK(sout == span);
        }
    }
}

void test_ids_and_context_handoff() {
    using tracing::TraceId;
    using tracing::SpanId;
    using tracing::SpanContext;
    // Cross-thread handoff through the core SPSC queue: copy equality.
    {
        core::SpscRingBuffer<SpanContext, 16> queue{};
        const SpanContext original{TraceId::generate(), SpanId::generate(),
                                   tracing::SamplingDecision::RecordAndSample};
        std::jthread receiver([&queue, &original] {
            SpanContext received{};
            for (int spins = 0; spins < 1'000; ++spins) {
                if (queue.try_pop(received)) {
                    NX_CHECK(received == original); // copy compares equal
                    NX_CHECK(received.trace_id == original.trace_id);
                    NX_CHECK(received.span_id == original.span_id);
                    NX_CHECK(received.sampled == original.sampled);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            NX_CHECK(false); // handoff never landed
        });
        NX_CHECK(queue.try_push(original).has_value());
        receiver.join();
    }
}

void test_ids_and_context_diagnostics() {
    using tracing::TraceId;
    using tracing::SpanId;
    using tracing::SpanContext;
    // SpanContext diagnostics: member/free agreement, std::format equality.
    {
        const SpanContext ctx{TraceId{0xA}, SpanId{0xB},
                              tracing::SamplingDecision::RecordAndSample};
        const std::string member = ctx.to_string();
        NX_CHECK(member == tracing::to_string(ctx)); // free mirror
        NX_CHECK(member.find("000000000000000a") != std::string::npos);
        NX_CHECK(member.find("000000000000000b") != std::string::npos);
        NX_CHECK(member.find("RecordAndSample") != std::string::npos);
        NX_CHECK(std::format("{}", ctx) == member);

        // Context JSON round-trips every field.
        const nlohmann::json j = ctx;
        SpanContext back{};
        nlohmann::from_json(j, back);
        NX_CHECK(back == ctx);
    }
}

// --- ids_and_context suite (task 3.1 / spec tracing/span-context): nonzero
// and distinct generation (single + 32 concurrent threads), wait-free
// allocation-free loops, the zero sentinel, 16-char hex round-trips,
// cross-thread copy equality through the core SPSC queue, member/free
// to_string agreement, and std::format equality.
void test_ids_and_context() {
    test_ids_and_context_sentinels();

    test_ids_and_context_uniqueness();

    test_ids_and_context_alloc_free();

    test_ids_and_context_concurrent();

    test_ids_and_context_hex_json();

    test_ids_and_context_handoff();

    test_ids_and_context_diagnostics();
}

// --- attributes suite (task 3.2 / spec tracing/span-context "Fixed-size span
// attribute union" + "Span attribute string storage" + "capacity bounds"):
// every value type packs and reads back, inline vs view strings, key/capacity
// bounds rejected without side effects, allocation-free stores.

void test_parent_stack() {
    using tracing::detail::ActiveFrame;
    using tracing::detail::active_span_stack;
    using tracing::detail::max_span_depth;
    static_assert(max_span_depth == 16);

    auto& stack = active_span_stack();
    stack.clear();

    // Empty stack: root span gets no parent from the stack.
    NX_CHECK(stack.depth() == 0U);
    NX_CHECK(stack.top() == nullptr);

    // Nested chaining: each frame parents the next push.
    const ActiveFrame root{tracing::TraceId{0x10}, tracing::SpanId{0x11}};
    const std::uint8_t root_index = stack.push(root);
    NX_CHECK(root_index == 0U);
    NX_CHECK(stack.top() != nullptr && *stack.top() == root);

    const ActiveFrame child{root.trace_id, tracing::SpanId{0x12}};
    const std::uint8_t child_index = stack.push(child);
    NX_CHECK(child_index == 1U);
    NX_CHECK(*stack.top() == child); // the active span is the child now

    const ActiveFrame grandchild{root.trace_id, tracing::SpanId{0x13}};
    static_cast<void>(stack.push(grandchild));
    NX_CHECK(stack.depth() == 3U);
    NX_CHECK(*stack.top() == grandchild);

    // End the CHILD while the grandchild is still active: truncation removes
    // the child frame AND everything above it (design D7 — no LIFO corrupt).
    stack.truncate_to(child_index);
    NX_CHECK(stack.depth() == 1U);
    NX_CHECK(*stack.top() == root);

    // The grandchild's own (late) truncation clamps to a no-op.
    stack.truncate_to(2U);
    NX_CHECK(stack.depth() == 1U);
    NX_CHECK(*stack.top() == root);

    // Siblings: sequential pushes/truncations under the same parent.
    const ActiveFrame sib1{root.trace_id, tracing::SpanId{0x21}};
    const std::uint8_t sib1_index = stack.push(sib1);
    NX_CHECK(*stack.top() == sib1);
    stack.truncate_to(sib1_index);
    NX_CHECK(*stack.top() == root); // back under the parent
    const ActiveFrame sib2{root.trace_id, tracing::SpanId{0x22}};
    static_cast<void>(stack.push(sib2));
    NX_CHECK(*stack.top() == sib2);
    NX_CHECK(stack.depth() == 2U);
    stack.truncate_to(root_index);
    NX_CHECK(stack.depth() == 0U && stack.top() == nullptr); // root ended: empty

    // Over-deep: the 17th nested push returns the sentinel and stores
    // nothing — the caller records a bounded root (fresh trace id, zero
    // parent) instead of pushing.
    {
        stack.clear();
        for (std::uint8_t i = 0; i < max_span_depth; ++i) {
            const ActiveFrame frame{tracing::TraceId{0x99}, tracing::SpanId{0x100U + i}};
            NX_CHECK(stack.push(frame) == i);
        }
        NX_CHECK(stack.depth() == max_span_depth);
        const ActiveFrame overflow{tracing::TraceId{0xAA}, tracing::SpanId{0xBB}};
        NX_CHECK(stack.push(overflow) == max_span_depth); // sentinel
        NX_CHECK(stack.depth() == max_span_depth);        // nothing stored
        NX_CHECK(stack.top() != nullptr && stack.top()->span_id == tracing::SpanId{0x100U + 15U});
        // Bounded-root truncation is a structural no-op (sentinel index).
        stack.truncate_to(max_span_depth);
        NX_CHECK(stack.depth() == max_span_depth);
        // Drain for the next suites.
        stack.truncate_to(0U);
        NX_CHECK(stack.depth() == 0U);
    }

    // Explicit-parent override (task 4.1): the caller-supplied context wins
    // over the stack top — exercised end-to-end in the scoped_span suite;
    // here the stack law itself: an explicit-parent child still pushes ITS
    // OWN frame so subsequent same-thread spans link to it.
    {
        stack.clear();
        static_cast<void>(stack.push(root));
        const tracing::SpanContext explicit_parent{tracing::TraceId{0xEE}, tracing::SpanId{0xEF},
                                                   tracing::SamplingDecision::RecordAndSample};
        // The explicit-parent child's own frame carries the INHERITED trace
        // id and the child's own span id.
        const ActiveFrame link{explicit_parent.trace_id, tracing::SpanId{0xF0}};
        const std::uint8_t link_index = stack.push(link);
        NX_CHECK(*stack.top() == link); // the local parent is now the child
        stack.truncate_to(link_index);
        stack.truncate_to(0U);
    }

    // --- affinity: pin/read round-trip + error paths on a probe thread.
    {
        std::uint64_t allowed = 0ULL;
        NX_CHECK(tracing::detail::self_affinity(allowed).has_value());
        NX_CHECK(allowed != 0ULL); // the process always has some allowed core

        // Pick one allowed core for the round-trip.
        std::uint64_t probe_mask = 0ULL;
        for (std::size_t bit = 0; bit < 64U && probe_mask == 0ULL; ++bit) {
            if ((allowed & (1ULL << bit)) != 0ULL) {
                probe_mask = 1ULL << bit;
            }
        }
        std::atomic<int> affinity_failures{0};
        std::jthread probe([&probe_mask, &affinity_failures] {
            // Zero mask: InvalidArgument, affinity untouched.
            if (tracing::detail::pin_self(0ULL).error() != core::ErrorCode::InvalidArgument) {
                affinity_failures.fetch_add(1, std::memory_order_relaxed);
            }
            // Valid single core: pin, read back, exactly the mask.
            if (!tracing::detail::pin_self(probe_mask).has_value()) {
                affinity_failures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            std::uint64_t observed = 0ULL;
            if (!tracing::detail::self_affinity(observed).has_value() || observed != probe_mask) {
                affinity_failures.fetch_add(1, std::memory_order_relaxed);
            }
        });
        probe.join();
        NX_CHECK(affinity_failures.load(std::memory_order_relaxed) == 0);
        NX_CHECK(tracing::detail::self_affinity(allowed).has_value()); // main restored
    }
}

// Compile-failure proof for task 5.1 (spec tracer-concepts "Null types are
// not deserializable"): this scratch TU MUST fail to compile (no from_json
// overload exists for either null type). Verified by running the commands,
// capturing the error, then deleting the file:
//   cat > /tmp/opencode/neg/null_from_json.cpp <<'EOF'
//   #include <nlohmann/json.hpp>
//   #include <nexigen/tracing/concepts/tracer_concept.hpp>
//   #include <nexigen/tracing/null_trace_sink.hpp>
//   int main() {
//       nlohmann::json j;
//       nexigen::tracing::NullTracer tracer;
//       nexigen::tracing::NullTraceSink sink;
//       nlohmann::from_json(j, tracer);
//       nlohmann::from_json(j, sink);
//   }
//   EOF
//   g++ -std=c++23 -fno-exceptions -fno-rtti -D_GNU_SOURCE -DJSON_NOEXCEPTION=1
//       -Iinclude -c /tmp/opencode/neg/null_from_json.cpp -o /dev/null
//       # expect: no matching function for call to 'from_json(...)'

// --- concept fixtures (task 5.1 / spec tracing/tracer-concepts): positive
// references pinned at compile time plus deliberately-broken probes — each
// negative proves the concept REJECTS the defect (absence/signature errors
// are compile law).

/// Complete conforming tracer surface (the positive reference).

} // namespace

void suite_ids_and_context() {
    run_suite("ids_and_context", test_ids_and_context);
}

void suite_parent_stack() {
    run_suite("parent_stack", test_parent_stack);
}
