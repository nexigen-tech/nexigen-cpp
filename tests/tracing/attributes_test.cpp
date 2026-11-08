// Split from tracing_test.cpp: nexigen::tracing span attributes.

#include "tracing_test_support.hpp"

namespace {

void test_attributes() {
    using tracing::SpanAttribute;
    using tracing::SpanAttributeType;

    // Every value type packs and reads back exactly.
    {
        SpanAttribute attr{};
        NX_CHECK(attr.store("i", std::int64_t{-42}));
        NX_CHECK(attr.type == SpanAttributeType::Int64);
        NX_CHECK(attr.value.i64 == -42);
        NX_CHECK(attr.key_view() == "i");

        NX_CHECK(attr.store("u", std::uint64_t{18'000'000'000'000'000'000ULL}));
        NX_CHECK(attr.type == SpanAttributeType::Uint64);
        NX_CHECK(attr.value.u64 == 18'000'000'000'000'000'000ULL);

        NX_CHECK(attr.store("f", 0.15625)); // exact binary fraction: bit-for-bit
        NX_CHECK(attr.type == SpanAttributeType::Double);
        NX_CHECK(attr.value.f64 == 0.15625);

        NX_CHECK(attr.store("b", true));
        NX_CHECK(attr.type == SpanAttributeType::Bool);
        NX_CHECK(attr.value.b);

        NX_CHECK(attr.store("s", std::string_view("exact")));
        NX_CHECK(attr.type == SpanAttributeType::String);
        NX_CHECK(attr.text_view() == "exact");
    }

    // Inline strings: copied in place, source mutation invisible (spec
    // scenario "Short strings copy inline").
    {
        char source[] = "mutable-buffer";
        SpanAttribute attr{};
        NX_CHECK(attr.store("k", std::string_view(source)));
        NX_CHECK(attr.text_inline);                     // inline arm (the 23-byte bound fits)
        source[0] = 'X';                                // clobber the caller's buffer
        NX_CHECK(attr.text_view() == "mutable-buffer"); // record holds its copy
    }

    // Long strings: zero-copy view, full text renders through the view (spec
    // scenario "Long string values are stored as views"); storage is a
    // program-static literal qualifying for the lifetime contract.
    {
        constexpr std::string_view long_text =
            "a-string-value-that-exceeds-the-23-byte-inline-bound-for-sure";
        static_assert(long_text.size() > tracing::attribute_inline_text_capacity);
        SpanAttribute attr{};
        NX_CHECK(attr.store("long", long_text));
        NX_CHECK(!attr.text_inline); // view arm, no bytes copied
        NX_CHECK(attr.text_view() == long_text);
        NX_CHECK(attr.text_view().data() == long_text.data()); // zero-copy
    }

    // The 23-byte boundary itself: exactly-23 is inline, exactly-24 is view.
    {
        constexpr std::string_view twenty3 = "abcdefghijklmnopqrstuvw"; // 23
        static_assert(twenty3.size() == 23);
        constexpr std::string_view twenty4 = "abcdefghijklmnopqrstuvwx"; // 24
        static_assert(twenty4.size() == 24);
        SpanAttribute a{}, b{};
        NX_CHECK(a.store("t", twenty3) && a.text_inline);
        NX_CHECK(b.store("t", twenty4) && !b.text_inline);
        NX_CHECK(b.text_view() == twenty4);
    }

    // Capacity bounds: MaxAttributes == 4 (pinned), a 5th store into a full
    // array is the caller's (ScopedSpan's) job — the attribute type itself
    // pins the key bound: a 16-char key is rejected WHOLE, no partial key.
    {
        static_assert(tracing::MaxAttributes == 4);
        SpanAttribute attr{};
        static_cast<void>(attr.store("ok", 1));
        const SpanAttribute before = attr;            // trivially copyable snapshot
        NX_CHECK(!attr.store("0123456789abcdef", 2)); // 16 chars: rejected
        NX_CHECK(std::memcmp(&attr, &before, sizeof(SpanAttribute)) == 0);
        NX_CHECK(attr.key_view() == "ok");          // untouched
        NX_CHECK(attr.store("0123456789abcde", 2)); // 15 chars: accepted
        NX_CHECK(attr.key_view() == "0123456789abcde");
    }

    // Trivially copyable: byte-for-byte copies survive (union pins at header
    // level are mirrored here for the test TU).
    static_assert(std::is_trivially_copyable_v<tracing::SpanAttribute>);
    static_assert(sizeof(tracing::SpanAttribute) == 48);

    // Stores never allocate.
    {
        const std::size_t arena_before = arena_used();
        SpanAttribute attr{};
        for (int i = 0; i < 10'000; ++i) {
            static_cast<void>(attr.store("k", std::int64_t{i}));
            static_cast<void>(attr.store("s", std::string_view("short")));
        }
        NX_CHECK(arena_used() == arena_before);
    }

    // Diagnostics + JSON round-trip per attribute type; hostile input leaves
    // the value untouched.
    {
        SpanAttribute attr{};
        static_cast<void>(attr.store("answer", std::int64_t{-42}));
        const std::string member = attr.to_string();
        NX_CHECK(member == tracing::to_string(attr));
        NX_CHECK(member.find("answer") != std::string::npos);
        NX_CHECK(member.find("Int64") != std::string::npos);
        NX_CHECK(std::format("{}", attr) == member);

        const nlohmann::json j = attr;
        NX_CHECK(j.at("key").get<std::string>() == "answer");
        NX_CHECK(j.at("type").get<std::string>() == "Int64");
        NX_CHECK(j.at("value").get<std::int64_t>() == -42);
        SpanAttribute back{};
        nlohmann::from_json(j, back);
        NX_CHECK(back.type == SpanAttributeType::Int64);
        NX_CHECK(back.value.i64 == -42);
        NX_CHECK(back.key_view() == "answer");

        // Every type through JSON.
        const std::array<nlohmann::json, 5> round = [] {
            SpanAttribute a1{}, a2{}, a3{}, a4{}, a5{};
            static_cast<void>(a1.store("i", std::int64_t{7}));
            static_cast<void>(a2.store("u", std::uint64_t{1ULL << 63}));
            static_cast<void>(a3.store("f", 2.5));
            static_cast<void>(a4.store("b", false));
            static_cast<void>(a5.store("s", std::string_view("text")));
            return std::array<nlohmann::json, 5>{a1, a2, a3, a4, a5};
        }();
        {
            SpanAttribute parsed{};
            nlohmann::from_json(round[0], parsed);
            NX_CHECK(parsed.type == SpanAttributeType::Int64 && parsed.value.i64 == 7);
            nlohmann::from_json(round[1], parsed);
            NX_CHECK(parsed.type == SpanAttributeType::Uint64 && parsed.value.u64 == (1ULL << 63));
            nlohmann::from_json(round[2], parsed);
            NX_CHECK(parsed.type == SpanAttributeType::Double && parsed.value.f64 == 2.5);
            nlohmann::from_json(round[3], parsed);
            NX_CHECK(parsed.type == SpanAttributeType::Bool && !parsed.value.b);
            nlohmann::from_json(round[4], parsed);
            NX_CHECK(parsed.type == SpanAttributeType::String && parsed.text_view() == "text");
        }

        // Hostile attribute JSON: scalars/arrays/null/wrong-typed values.
        SpanAttribute hostile_target{};
        static_cast<void>(hostile_target.store("keep", 5));
        const SpanAttribute hostile_before = hostile_target;
        const std::array<nlohmann::json, 4> hostile{
            nlohmann::json(1), nlohmann::json{1, 2}, nlohmann::json(nullptr),
            nlohmann::json{{"key", "k"}, {"type", "Int64"}, {"value", "not-a-number"}}};
        for (const nlohmann::json& bad : hostile) {
            nlohmann::from_json(bad, hostile_target);
            NX_CHECK(std::memcmp(&hostile_target, &hostile_before, sizeof(SpanAttribute)) == 0);
        }
    }
}

// --- span_record suite (task 3.2 / spec tracing/span-context
// "Cache-line-aligned span record"): compile-time pins, lossless field
// copies, JSON round-trip with hex ids and typed attributes, hostile JSON
// leaving the record unchanged, member/free to_string agreement.

} // namespace

void suite_attributes() {
    run_suite("attributes", test_attributes);
}
