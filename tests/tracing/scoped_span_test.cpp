// Split from tracing_test.cpp: nexigen::tracing ScopedSpan lifecycle.

#include "tracing_test_support.hpp"

namespace {

static_assert(tracing::concepts::Tracer<tracing::Tracer<64, 4>>);
static_assert(!std::is_default_constructible_v<tracing::ScopedSpan<64, 4>>);
static_assert(
    !std::is_constructible_v<tracing::ScopedSpan<64, 4>, std::string_view, tracing::SpanKind>);
static_assert(!std::is_copy_constructible_v<tracing::ScopedSpan<64, 4>>);
static_assert(!std::is_move_constructible_v<tracing::ScopedSpan<64, 4>>);
static_assert(!std::is_default_constructible_v<tracing::Tracer<64, 4>>);
static_assert(!std::is_constructible_v<tracing::Tracer<64, 4>, const char*>);
static_assert(!std::is_constructible_v<tracing::SpanPasskey>);

// Forward declarations: the scenario helpers are defined after
// test_scoped_span() so the file order of the suite's NX_CHECK statements
// matches the original inline layout (same order as the numbered blocks).
void test_scoped_span_lifecycle(tracing::Tracer<1024, 4>* tracer,
                                tracing::TracerProvider<1024, 4>* provider, RecordingSink& sink);
void test_scoped_span_explicit_end(tracing::Tracer<1024, 4>* tracer,
                                   tracing::TracerProvider<1024, 4>* provider, RecordingSink& sink);
void test_scoped_span_rejected_updates(tracing::Tracer<1024, 4>* tracer,
                                       tracing::TracerProvider<1024, 4>* provider,
                                       RecordingSink& sink);
void test_scoped_span_attribute_bounds(tracing::Tracer<1024, 4>* tracer,
                                       tracing::TracerProvider<1024, 4>* provider,
                                       RecordingSink& sink);
void test_scoped_span_string_attributes(tracing::Tracer<1024, 4>* tracer,
                                        tracing::TracerProvider<1024, 4>* provider,
                                        RecordingSink& sink);
void test_scoped_span_held_duration(tracing::Tracer<1024, 4>* tracer,
                                    tracing::TracerProvider<1024, 4>* provider,
                                    RecordingSink& sink);
void test_scoped_span_context(tracing::Tracer<1024, 4>* tracer,
                              tracing::TracerProvider<1024, 4>* provider, RecordingSink& sink);
void test_scoped_span_parent_chain(tracing::Tracer<1024, 4>* tracer,
                                   tracing::TracerProvider<1024, 4>* provider, RecordingSink& sink);
void test_scoped_span_explicit_parent(tracing::Tracer<1024, 4>* tracer,
                                      tracing::TracerProvider<1024, 4>* provider,
                                      RecordingSink& sink);
void test_scoped_span_depth_overflow(tracing::Tracer<1024, 4>* tracer,
                                     tracing::TracerProvider<1024, 4>* provider,
                                     RecordingSink& sink);
void test_scoped_span_end_truncation(tracing::Tracer<1024, 4>* tracer,
                                     tracing::TracerProvider<1024, 4>* provider,
                                     RecordingSink& sink);
void test_scoped_span_cross_thread(tracing::Tracer<1024, 4>* tracer,
                                   tracing::TracerProvider<1024, 4>* provider, RecordingSink& sink);
void test_scoped_span_disabled_short_circuit(tracing::Tracer<1024, 4>* tracer,
                                             tracing::TracerProvider<1024, 4>* provider,
                                             RecordingSink& sink,
                                             const tracing::DroppedStats& drops_before);
void test_scoped_span_sampling_drop(tracing::Tracer<1024, 4>* tracer,
                                    tracing::TracerProvider<1024, 4>* provider, RecordingSink& sink,
                                    const tracing::DroppedStats& drops_before);
void test_scoped_span_serialization(tracing::Tracer<1024, 4>* tracer,
                                    tracing::TracerProvider<1024, 4>* provider,
                                    RecordingSink& sink);
void test_scoped_span_alloc_free(tracing::Tracer<1024, 4>* tracer,
                                 tracing::TracerProvider<1024, 4>* provider, RecordingSink& sink);
void test_scoped_span_control_flips(const core::Result<tracing::Tracer<1024, 4>*>& tracer,
                                    tracing::TracerProvider<1024, 4>* provider,
                                    RecordingSink& sink);

// --- scoped_span suite (task 6.2 / spec tracing/scoped-span): the RAII
// lifecycle against a recording sink — exactly-once enqueue, idempotent
// end(), post-end rejections, bounds, default status, the full parent
// chain vocabulary (nested/sibling/explicit-override/root/overflow/
// end-with-active-children), cross-thread linking, context extraction,
// disabled/dropped short-circuits with mid-run flips, serialization pins,
// and the allocation-free window. All spans run on ONE dedicated producer
// thread: fresh TLS slot map (this TU constructs many providers on main)
// and a fresh active-span stack.
void test_scoped_span() {
    // ShardCapacity 1024: the tight loops below burst hundreds of spans
    // between flushes; a small ring would turn consumer wake latency into
    // buffer_full noise unrelated to what this suite verifies.
    using Provider = tracing::TracerProvider<1024, 4>;

    RecordingSink sink{};
    alignas(Provider) static std::byte storage[sizeof(Provider)];
    auto* provider =
        new (static_cast<void*>(storage)) Provider(tracing::TracerProviderConfig{}, sink);
    NX_CHECK(provider->initialize().has_value());
    const auto tracer = provider->get_tracer("scoped");
    NX_CHECK(tracer.has_value());

    std::jthread producer([tracer = *tracer, provider, &sink] {
        test_scoped_span_lifecycle(tracer, provider, sink);

        test_scoped_span_explicit_end(tracer, provider, sink);

        test_scoped_span_rejected_updates(tracer, provider, sink);

        test_scoped_span_attribute_bounds(tracer, provider, sink);

        test_scoped_span_string_attributes(tracer, provider, sink);

        test_scoped_span_held_duration(tracer, provider, sink);

        test_scoped_span_context(tracer, provider, sink);

        test_scoped_span_parent_chain(tracer, provider, sink);

        test_scoped_span_explicit_parent(tracer, provider, sink);

        test_scoped_span_depth_overflow(tracer, provider, sink);

        test_scoped_span_end_truncation(tracer, provider, sink);

        test_scoped_span_cross_thread(tracer, provider, sink);

        const tracing::DroppedStats drops_before = provider->dropped();
        test_scoped_span_disabled_short_circuit(tracer, provider, sink, drops_before);

        test_scoped_span_sampling_drop(tracer, provider, sink, drops_before);

        test_scoped_span_serialization(tracer, provider, sink);

        test_scoped_span_alloc_free(tracer, provider, sink);
    });
    producer.join();

    test_scoped_span_control_flips(tracer, provider, sink);

    std::destroy_at(provider);
}

void test_scoped_span_lifecycle(tracing::Tracer<1024, 4>* tracer,
                                tracing::TracerProvider<1024, 4>* provider, RecordingSink& sink) {
    using tracing::SpanRecord;
    {
        // (1) Destruction enqueues exactly one record (name/kind/location).
        {
            std::size_t drained = sink.count;
            static_cast<void>(drained);
            {
                auto span = tracer->start_span("lifecycle", tracing::SpanKind::Server, nullptr,
                                               std::source_location::current());
                NX_CHECK(span.active());
            } // destruction enqueues
            NX_CHECK(provider->flush().has_value());
            NX_CHECK(sink.count == 1);
            const SpanRecord& record = sink.view()[0];
            NX_CHECK(record.name_view() == "lifecycle");
            NX_CHECK(record.kind == tracing::SpanKind::Server);
            NX_CHECK(record.flags == 0U); // destruction-ended, not explicit
            NX_CHECK(std::string_view(record.file_name).find("_test.cpp") !=
                     std::string_view::npos);
            NX_CHECK(record.line > 0U);
        }
    }
}

void test_scoped_span_explicit_end(tracing::Tracer<1024, 4>* tracer,
                                   tracing::TracerProvider<1024, 4>* provider,
                                   RecordingSink& sink) {
    using tracing::SpanRecord;
    {
        // (2) Explicit end(): enqueues once, destruction does not push a
        // second record, subsequent end() calls are no-ops (spec "Explicit
        // end does not double-enqueue").
        {
            auto span = tracer->start_span("explicit", tracing::SpanKind::Client);
            NX_CHECK(span.end());  // true: this call ended it
            NX_CHECK(!span.end()); // no-op
            NX_CHECK(!span.end());
            NX_CHECK(provider->flush().has_value());
            NX_CHECK(sink.count == 2);
            const SpanRecord& record = sink.view()[1];
            NX_CHECK(record.name_view() == "explicit");
            NX_CHECK((record.flags & SpanRecord::flag_ended_explicit) != 0U);
        } // destruction: no second push (count stays 2 through the flush)
    }
}

void test_scoped_span_rejected_updates(tracing::Tracer<1024, 4>* tracer,
                                       tracing::TracerProvider<1024, 4>* provider,
                                       RecordingSink& sink) {
    using tracing::SpanRecord;
    {
        // (3) Post-end updates are rejected, mutate nothing (spec "Updates
        // after end are rejected").
        {
            auto span = tracer->start_span("rejected", tracing::SpanKind::Internal);
            static_cast<void>(span.set_attribute("early", 1));
            NX_CHECK(span.end());
            NX_CHECK(!span.set_attribute("late", 2));
            NX_CHECK(!span.set_status(tracing::SpanStatus::Error));
            NX_CHECK(provider->flush().has_value());
            const SpanRecord& record = sink.view()[sink.count - 1];
            NX_CHECK(record.attribute_count == 1); // only the early one
            NX_CHECK(record.attributes[0].key_view() == "early");
            NX_CHECK(record.status == tracing::SpanStatus::Unset); // unchanged
        }
    }
}

void test_scoped_span_attribute_bounds(tracing::Tracer<1024, 4>* tracer,
                                       tracing::TracerProvider<1024, 4>* provider,
                                       RecordingSink& sink) {
    using tracing::SpanRecord;
    {
        // (4) Attribute capacity (MaxAttributes = 4) and key bounds rejected
        // without side effects; the first four intact (spec "bounds").
        // NOTE: records enqueue at END — the span is closed before the
        // flush/inspect step.
        {
            {
                auto span = tracer->start_span("attrs", tracing::SpanKind::Internal);
                NX_CHECK(span.set_attribute("one", std::int64_t{1}));
                NX_CHECK(span.set_attribute("two", std::uint64_t{2}));
                NX_CHECK(span.set_attribute("three", 3.5));
                NX_CHECK(span.set_attribute("four", true));
                NX_CHECK(!span.set_attribute("fifth", 5));            // capacity bound
                NX_CHECK(!span.set_attribute("0123456789abcdef", 6)); // key > 15
                // Every value type reads back exactly from the drained record.
                NX_CHECK(span.set_status(tracing::SpanStatus::Ok));
            }
            NX_CHECK(provider->flush().has_value());
            const SpanRecord& record = sink.view()[sink.count - 1];
            NX_CHECK(record.attribute_count == 4);
            NX_CHECK(record.attributes[0].type == tracing::SpanAttributeType::Int64 &&
                     record.attributes[0].value.i64 == 1);
            NX_CHECK(record.attributes[1].type == tracing::SpanAttributeType::Uint64 &&
                     record.attributes[1].value.u64 == 2);
            NX_CHECK(record.attributes[2].type == tracing::SpanAttributeType::Double &&
                     record.attributes[2].value.f64 == 3.5);
            NX_CHECK(record.attributes[3].type == tracing::SpanAttributeType::Bool &&
                     record.attributes[3].value.b);
            NX_CHECK(record.status == tracing::SpanStatus::Ok);
        }
    }
}

void test_scoped_span_string_attributes(tracing::Tracer<1024, 4>* tracer,
                                        tracing::TracerProvider<1024, 4>* provider,
                                        RecordingSink& sink) {
    using tracing::SpanRecord;
    {
        // (5) Default status Unset + inline/view strings land (spec
        // "Attributes and status land in the drained record").
        {
            {
                auto span = tracer->start_span("strings", tracing::SpanKind::Consumer);
                char mutable_source[] = "short-lived";
                NX_CHECK(span.set_attribute("inline", std::string_view(mutable_source)));
                mutable_source[0] = 'X'; // record must hold its own copy
                NX_CHECK(span.set_attribute(
                    "view", std::string_view("a-program-static-view-longer-than-23-bytes")));
            }
            NX_CHECK(provider->flush().has_value());
            const SpanRecord& record = sink.view()[sink.count - 1];
            NX_CHECK(record.status == tracing::SpanStatus::Unset);
            NX_CHECK(record.attributes[0].text_view() == "short-lived");
            NX_CHECK(record.attributes[1].text_view() ==
                     "a-program-static-view-longer-than-23-bytes");
        }
    }
}

void test_scoped_span_held_duration(tracing::Tracer<1024, 4>* tracer,
                                    tracing::TracerProvider<1024, 4>* provider,
                                    RecordingSink& sink) {
    using tracing::SpanRecord;
    {
        // (6) Construction stamps the start; held duration survives into
        // end-start (spec "Construction stamps the start").
        {
            auto span = tracer->start_span("held", tracing::SpanKind::Internal);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            static_cast<void>(span.end());
            NX_CHECK(provider->flush().has_value());
            const SpanRecord& record = sink.view()[sink.count - 1];
            NX_CHECK(record.end > record.start);
            NX_CHECK(record.duration_ns() >= 1'500'000);
        }
    }
}

void test_scoped_span_context(tracing::Tracer<1024, 4>* tracer,
                              tracing::TracerProvider<1024, 4>* provider, RecordingSink& sink) {
    using tracing::SpanRecord;
    {
        // (7) context() matches the drained record; sampled reflects the
        // decision at start (spec "Extracted context matches the drained
        // record").
        {
            auto span = tracer->start_span("context", tracing::SpanKind::Producer);
            const tracing::SpanContext ctx = span.context();
            static_cast<void>(span.end());
            NX_CHECK(provider->flush().has_value());
            const SpanRecord& record = sink.view()[sink.count - 1];
            NX_CHECK(ctx.trace_id == record.trace_id);
            NX_CHECK(ctx.span_id == record.span_id);
            NX_CHECK(ctx.sampled == tracing::SamplingDecision::RecordAndSample);
        }
    }
}

void test_scoped_span_parent_chain(tracing::Tracer<1024, 4>* tracer,
                                   tracing::TracerProvider<1024, 4>* provider,
                                   RecordingSink& sink) {
    using tracing::SpanRecord;
    {
        // (8) Automatic parent chains: nested → child inherits trace and
        // parents to the active span; siblings share the trace and parent
        // (spec "Nested spans chain parent ids" / "Siblings share the trace
        // id"). This thread's stack is empty here: the first span is a root.
        const std::size_t base = sink.count;
        tracing::SpanId root_id{};
        tracing::TraceId root_trace{};
        {
            auto root = tracer->start_span("root", tracing::SpanKind::Internal);
            root_trace = root.context().trace_id;
            root_id = root.context().span_id;
            {
                auto child = tracer->start_span("child", tracing::SpanKind::Server);
                NX_CHECK(child.context().trace_id == root_trace);
                {
                    auto grandchild = tracer->start_span("grandchild", tracing::SpanKind::Client);
                    NX_CHECK(grandchild.context().trace_id == root_trace);
                }
            } // child and grandchild end first (LIFO)
            {
                auto sib1 = tracer->start_span("sib1", tracing::SpanKind::Internal);
                NX_CHECK(sib1.context().trace_id == root_trace);
            }
            {
                auto sib2 = tracer->start_span("sib2", tracing::SpanKind::Internal);
                NX_CHECK(sib2.context().trace_id == root_trace);
            }
        }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(sink.count == base + 5);
        { // Records arrive in END order: grandchild, child, sib1, sib2, root.
            const SpanRecord& grand_rec = sink.view()[base];
            const SpanRecord& child_rec = sink.view()[base + 1];
            const SpanRecord& sib1_rec = sink.view()[base + 2];
            const SpanRecord& sib2_rec = sink.view()[base + 3];
            const SpanRecord& root_rec = sink.view()[base + 4];
            NX_CHECK(root_rec.trace_id == root_trace);
            NX_CHECK(root_rec.parent_span_id.is_unset()); // fresh root
            NX_CHECK(child_rec.trace_id == root_trace);
            NX_CHECK(child_rec.parent_span_id == root_id);
            NX_CHECK(grand_rec.trace_id == root_trace);
            NX_CHECK(grand_rec.parent_span_id == child_rec.span_id);
            NX_CHECK(sib1_rec.trace_id == root_trace && sib2_rec.trace_id == root_trace);
            NX_CHECK(sib1_rec.parent_span_id == root_id);
            NX_CHECK(sib2_rec.parent_span_id == root_id);
        }
    }
}

void test_scoped_span_explicit_parent(tracing::Tracer<1024, 4>* tracer,
                                      tracing::TracerProvider<1024, 4>* provider,
                                      RecordingSink& sink) {
    using tracing::SpanRecord;
    {
        // (9) Explicit parent overrides the stack; the explicit-parent child
        // becomes the LOCAL parent (spec "Explicit parent overrides the
        // stack" + "The child becomes the local parent").
        const std::size_t ebase = sink.count;
        {
            auto active = tracer->start_span("active", tracing::SpanKind::Internal);
            const tracing::SpanContext foreign{tracing::TraceId{0xABCD}, tracing::SpanId{0xEF01},
                                               tracing::SamplingDecision::RecordAndSample};
            {
                auto linked = tracer->start_span("linked", tracing::SpanKind::Consumer, &foreign);
                NX_CHECK(linked.context().trace_id == foreign.trace_id);
                // A further span on THIS thread links to the child now.
                auto deeper = tracer->start_span("deeper", tracing::SpanKind::Internal);
                NX_CHECK(deeper.context().trace_id == foreign.trace_id);
            }
        }
        NX_CHECK(provider->flush().has_value());
        { // END order: deeper, linked, active.
            const SpanRecord& deeper_rec = sink.view()[ebase];
            const SpanRecord& linked_rec = sink.view()[ebase + 1];
            NX_CHECK(linked_rec.trace_id == tracing::TraceId{0xABCD});
            NX_CHECK(linked_rec.parent_span_id == tracing::SpanId{0xEF01});
            NX_CHECK(deeper_rec.trace_id == tracing::TraceId{0xABCD});
            NX_CHECK(deeper_rec.parent_span_id == linked_rec.span_id);
        }
    }
}

void test_scoped_span_depth_overflow(tracing::Tracer<1024, 4>* tracer,
                                     tracing::TracerProvider<1024, 4>* provider,
                                     RecordingSink& sink) {
    using tracing::SpanRecord;
    {
        // (10) Depth overflow records as a bounded root: 16 nested spans
        // fill the stack; the 17th gets a FRESH trace id and zero parent
        // (spec "Root spans and depth overflow are bounded"). The guards are
        // held through recursion (each frame owns one local; the type is
        // non-movable so no container can hold them).
        const std::size_t obase = sink.count;
        tracing::TraceId overflow_trace{};
        {
            auto nest = [&](auto&& self, int depth) -> void {
                if (depth < 16) {
                    auto span = tracer->start_span("deep", tracing::SpanKind::Internal);
                    self(self, depth + 1);
                    return;
                }
                auto overflow = tracer->start_span("overflow", tracing::SpanKind::Internal);
                NX_CHECK(overflow.active()); // still recorded
                overflow_trace = overflow.context().trace_id;
                NX_CHECK(overflow_trace != sink.view()[obase].trace_id);
                static_cast<void>(overflow.end());
            };
            nest(nest, 0);
        }
        NX_CHECK(provider->flush().has_value());
        {
            // END order: overflow (explicit end) first, then the deep spans
            // as the recursion unwinds — innermost (deep15) before deep0.
            // view[obase+i] is deep[16-i]: its parent is deep[15-i], the
            // record at obase+i+1; deep0 (view[obase+16]) is the root.
            const SpanRecord& overflow_rec = sink.view()[obase];
            NX_CHECK(overflow_rec.name_view() == "overflow");
            NX_CHECK(overflow_rec.parent_span_id.is_unset()); // bounded root
            NX_CHECK(overflow_rec.trace_id == overflow_trace);
            for (std::size_t i = 1; i <= 16; ++i) {
                const SpanRecord& deep_rec = sink.view()[obase + i];
                NX_CHECK(deep_rec.name_view() == "deep");
                if (i < 16) {
                    NX_CHECK(deep_rec.parent_span_id == sink.view()[obase + i + 1].span_id);
                } else {
                    NX_CHECK(deep_rec.parent_span_id.is_unset()); // deep0 = root
                }
            }
        }
    }
}

void test_scoped_span_end_truncation(tracing::Tracer<1024, 4>* tracer,
                                     tracing::TracerProvider<1024, 4>* provider,
                                     RecordingSink& sink) {
    using tracing::SpanRecord;
    {
        // (11) Explicit end() with active children truncates their frames:
        // the next span parents to the surviving stack top (root here), and
        // the child's later destruction is a coherent no-op (design D7).
        const std::size_t tbase = sink.count;
        {
            auto parent = tracer->start_span("parent", tracing::SpanKind::Internal);
            auto child = tracer->start_span("child", tracing::SpanKind::Internal);
            NX_CHECK(parent.end()); // child's frame discarded early
            auto next = tracer->start_span("after-truncation", tracing::SpanKind::Internal);
            static_cast<void>(next);
            static_cast<void>(child); // later end: clamped no-op
        }
        NX_CHECK(provider->flush().has_value());
        { // END order: parent (explicit end()), then next, then child.
            const SpanRecord& parent_rec = sink.view()[tbase];
            const SpanRecord& next_rec = sink.view()[tbase + 1];
            const SpanRecord& child_rec = sink.view()[tbase + 2];
            NX_CHECK(child_rec.parent_span_id == parent_rec.span_id); // chained before
            NX_CHECK(next_rec.parent_span_id.is_unset());             // stack truncated to root
        }
    }
}

void test_scoped_span_cross_thread(tracing::Tracer<1024, 4>* tracer,
                                   tracing::TracerProvider<1024, 4>* provider,
                                   RecordingSink& sink) {
    using tracing::SpanRecord;
    {
        // (12) Cross-thread context linking through the core SPSC queue
        // (spec "Child links to a context produced on another thread"):
        // this thread hands a live span's context to a worker, which starts
        // the explicit-parent child; a deeper span there auto-links to it.
        {
            auto live = tracer->start_span("cross-root", tracing::SpanKind::Internal);
            const tracing::SpanContext ctx = live.context();
            core::SpscRingBuffer<tracing::SpanContext, 4> handoff{};
            NX_CHECK(handoff.try_push(ctx).has_value());
            std::jthread worker([tracer, &handoff, &sink, provider] {
                tracing::SpanContext received{};
                for (int spins = 0; spins < 1'000; ++spins) {
                    if (handoff.try_pop(received)) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                auto child =
                    tracer->start_span("cross-child", tracing::SpanKind::Server, &received);
                NX_CHECK(child.context().trace_id == received.trace_id);
                {
                    auto deeper = tracer->start_span("cross-deeper", tracing::SpanKind::Client);
                    NX_CHECK(deeper.context().trace_id == received.trace_id);
                    NX_CHECK(deeper.context().span_id != received.span_id);
                }
                static_cast<void>(provider->flush());
                static_cast<void>(&sink);
            });
            worker.join();
            static_cast<void>(live.end());
            NX_CHECK(provider->flush().has_value());
        }
    }
}

void test_scoped_span_disabled_short_circuit(tracing::Tracer<1024, 4>* tracer,
                                             tracing::TracerProvider<1024, 4>* provider,
                                             RecordingSink& sink,
                                             const tracing::DroppedStats& drops_before) {
    using tracing::SpanRecord;
    {
        // (13) Disabled short-circuit: no record, no stack touch — a span
        // dropped INSIDE an active parent leaves the parent linkable
        // afterwards; drop counters stay zero (spec "Disabled provider emits
        // nothing" + "Dropping is not a drop counter increment").
        const std::size_t dbase = sink.count;
        {
            auto parent = tracer->start_span("disable-parent", tracing::SpanKind::Internal);
            provider->set_enabled(false);
            {
                auto dropped = tracer->start_span("dropped", tracing::SpanKind::Internal);
                NX_CHECK(!dropped.active());
                NX_CHECK(dropped.error() == core::ErrorCode::Ok);
            }
            provider->set_enabled(true);
            auto after = tracer->start_span("after-drop", tracing::SpanKind::Internal);
            NX_CHECK(after.context().trace_id == parent.context().trace_id);
            NX_CHECK(after.context().span_id != parent.context().span_id);
        }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(sink.count == dbase + 2); // parent + after-drop only
        {                                  // END order: after-drop, then parent.
            const SpanRecord& parent_rec = sink.view()[dbase + 1];
            const SpanRecord& after_rec = sink.view()[dbase];
            NX_CHECK(after_rec.parent_span_id == parent_rec.span_id);
        }
        NX_CHECK(provider->dropped().rate_limited == drops_before.rate_limited);
        NX_CHECK(provider->dropped().buffer_full == drops_before.buffer_full);
        NX_CHECK(provider->dropped().slot_exhausted == drops_before.slot_exhausted);
    }
}

void test_scoped_span_sampling_drop(tracing::Tracer<1024, 4>* tracer,
                                    tracing::TracerProvider<1024, 4>* provider, RecordingSink& sink,
                                    const tracing::DroppedStats& drops_before) {
    using tracing::SpanRecord;
    {
        // (14) Sampling Drop short-circuits per span; flipping back records
        // normally (spec "Sampling Drop short-circuits per span").
        const std::size_t sbase = sink.count;
        provider->set_sampling(tracing::SamplingDecision::Drop);
        {
            auto filtered = tracer->start_span("filtered", tracing::SpanKind::Internal);
            NX_CHECK(!filtered.active());
            NX_CHECK(filtered.context() == tracing::SpanContext{});
        }
        provider->set_sampling(tracing::SamplingDecision::RecordAndSample);
        {
            auto recorded = tracer->start_span("recorded", tracing::SpanKind::Internal);
            NX_CHECK(recorded.active());
        }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(sink.count == sbase + 1);
        NX_CHECK(sink.view()[sbase].name_view() == "recorded");
        NX_CHECK(provider->dropped().total() == drops_before.total()); // still zero
    }
}

void test_scoped_span_serialization(tracing::Tracer<1024, 4>* tracer,
                                    tracing::TracerProvider<1024, 4>* provider,
                                    RecordingSink& sink) {
    static_cast<void>(provider);
    static_cast<void>(sink);
    using tracing::SpanRecord;
    {
        // (15) ScopedSpan serialization pins (spec "Guard renders its
        // state"): to_string/formatter agree; to_json serializes live state.
        {
            auto span = tracer->start_span("serialize", tracing::SpanKind::Server);
            static_cast<void>(span.set_status(tracing::SpanStatus::Error));
            const std::string text = span.to_string();
            NX_CHECK(text == tracing::to_string(span));
            NX_CHECK(std::format("{}", span) == text);
            NX_CHECK(text.find("serialize") != std::string::npos);
            NX_CHECK(text.find("Server") != std::string::npos);
            NX_CHECK(text.find("Error") != std::string::npos);
            const nlohmann::json j = span;
            NX_CHECK(j.at("name").get<std::string>() == "serialize");
            NX_CHECK(j.at("kind").get<std::string>() == "Server");
            NX_CHECK(j.at("status").get<std::string>() == "Error");
            NX_CHECK(j.at("active").get<bool>());
            static_cast<void>(span.end());
        }
    }
}

void test_scoped_span_alloc_free(tracing::Tracer<1024, 4>* tracer,
                                 tracing::TracerProvider<1024, 4>* provider, RecordingSink& sink) {
    static_cast<void>(sink);
    using tracing::SpanRecord;
    {
        // (16) Updates never allocate or block: a tight loop with both
        // counters armed (slot/TLS warmed by everything above).
        {
            const std::size_t arena_before = arena_used();
            const std::size_t heap_before = heap_in_use();
            for (int i = 0; i < 5'000; ++i) {
                auto span = tracer->start_span("alloc-free", tracing::SpanKind::Internal);
                static_cast<void>(span.set_attribute("k", i));
                static_cast<void>(span.set_status(tracing::SpanStatus::Ok));
                if ((i % 256) == 255) {
                    // Keep the ring shallow across the burst: the loop
                    // measures allocation freedom, not queueing pressure.
                    static_cast<void>(provider->flush());
                }
            }
            static_cast<void>(provider->flush());
            NX_CHECK(arena_used() == arena_before);
            NX_CHECK(heap_in_use() == heap_before);
        }
    }
}

void test_scoped_span_control_flips(const core::Result<tracing::Tracer<1024, 4>*>& tracer,
                                    tracing::TracerProvider<1024, 4>* provider,
                                    RecordingSink& sink) {
    // Control flips under concurrency (spec "Control reads need no
    // synchronization"): one thread flips enable/sampling while another
    // records; exactly the active-at-start spans drain. The flush BEFORE
    // the baseline matters: the worker may still be draining the previous
    // batch's tail when producer.join() returned.
    {
        NX_CHECK(provider->flush().has_value());
        const std::uint64_t drops_at_base = provider->dropped().buffer_full;
        std::atomic<bool> stop_flip{false};
        std::atomic<int> active_starts{0};
        std::jthread flipper([provider, &stop_flip] {
            bool enabled = true;
            bool drop = false;
            while (!stop_flip.load(std::memory_order_relaxed)) {
                provider->set_enabled(enabled);
                provider->set_sampling(drop ? tracing::SamplingDecision::Drop
                                            : tracing::SamplingDecision::RecordAndSample);
                enabled = !enabled;
                drop = !drop;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            provider->set_enabled(true);
            provider->set_sampling(tracing::SamplingDecision::RecordAndSample);
        });
        const std::size_t flip_base = sink.count;
        std::jthread recorder([tracer = *tracer, &active_starts] {
            for (int i = 0; i < 2'000; ++i) {
                auto span = tracer->start_span("flips", tracing::SpanKind::Internal);
                if (span.active()) {
                    active_starts.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
        recorder.join();
        stop_flip.store(true, std::memory_order_relaxed);
        flipper.join();
        NX_CHECK(provider->flush().has_value());
        const std::size_t expected =
            flip_base + static_cast<std::size_t>(active_starts.load(std::memory_order_relaxed));
        // Burst-tolerant invariant: every active-at-start span either
        // drained or was counted buffer_full (a full shard drops by design —
        // the producer never blocks; Debug bursts can outrun the consumer).
        const std::uint64_t drops_during = provider->dropped().buffer_full - drops_at_base;
        if (sink.count + static_cast<std::size_t>(drops_during) != expected) {
            std::printf("flips MISMATCH: base=%zu active=%d count=%zu dropped=%s\n", flip_base,
                        active_starts.load(std::memory_order_relaxed), sink.count,
                        provider->dropped().to_string().c_str());
        }
        NX_CHECK(sink.count + static_cast<std::size_t>(drops_during) == expected);
        sink.reset();
    }
}

} // namespace

void suite_scoped_span() {
    run_suite("scoped_span", test_scoped_span);
}
