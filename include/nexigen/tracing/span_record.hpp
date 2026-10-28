// nexigen::tracing — the record that crosses the producer/consumer boundary
// (design D5; spec tracing/span-context "Fixed-size span attribute union" and
// "Cache-line-aligned span record").
//
// SpanRecord is the fixed-size, cache-line-aligned, trivially copyable value
// stored in the per-thread SPSC shards: ids, parent, kind, status, cached
// thread id, start/end monotonic timestamps, source location, the inline
// span name, and the inline attribute array. The producer fills it with
// fixed-offset stores; NOTHING renders on the hot path — the consumer/sink
// renders cold-side.
//
// Pinned layout (design D5), pinned by static_assert:
//   0   TraceId        trace_id          8 B (strong uint64)
//   8   SpanId         span_id           8 B
//  16   SpanId         parent_span_id    8 B (zero = root)
//  24   SpanKind       kind              1 B
//  25   SpanStatus     status            1 B (default Unset)
//  26   uint8_t        flags             1 B (bit0: ended via explicit end())
//  27   uint8_t        attribute_count   1 B (≤ MaxAttributes = 4)
//  28   uint32_t       thread_id         4 B (cached SYS_gettid)
//  32   Timestamp      start             8 B (monotonic ns)
//  40   Timestamp      end               8 B (monotonic ns)
//  48   const char*    file_name         8 B (source_location static storage)
//  56   const char*    function_name     8 B (static storage)
//  64   uint32_t       line              4 B
//  68   uint32_t       column            4 B
//  72   char           name[48]         48 B (≤ 47 chars + NUL, bounded copy)
// 120   SpanAttribute  attributes[4]   192 B (4 × 48)
// 312   (8 B tail padding to alignas(64))  → sizeof == 320 == 5 cache lines
//
// SpanAttribute (design D5), 48 bytes, trivially copyable, alignas(8):
//   0   char           key[16]           inline bounded key (≤ 15 chars + NUL;
//                                          longer keys are rejected WHOLE)
//  16   SpanAttributeType type           1 B tag + 7 B padding
//  24   union (24 B, 8-aligned):
//          int64_t i64; uint64_t u64; double f64; bool b;
//          struct { char text[23]; uint8_t length; } inline_text;   // ≤23 B
//          struct { const char* data; uint32_t length; } view;      // zero-copy
//
// STRING LIFETIME CONTRACT: values ≤ 23 bytes are ALWAYS copied into
// inline_text; longer values are stored as a zero-copy `view` over storage
// the CALLER guarantees outlives the span's drain (string literals and
// program-static tables qualify — the same lifetime class as LogRecord's
// fmt pointer). The contract is stated on the store surface itself.
//
// The serialization trio (mandates 9–11) is declared here; the allocating
// cold bodies live in src/tracing/record_format.cpp (design D2).
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/tracing/span_context.hpp>
#include <nexigen/tracing/span_enums.hpp>

namespace nexigen::tracing {

/// Inline attribute capacity per span (request-mandated spelling, design D3).
inline constexpr std::size_t MaxAttributes = 4;

/// Type tag of a SpanAttribute value (enum contract of mandates 9–11).
enum class SpanAttributeType : std::uint8_t {
    Int64,  ///< std::int64_t payload.
    Uint64, ///< std::uint64_t payload.
    Double, ///< double payload (bit-for-bit through copies/JSON).
    Bool,   ///< bool payload.
    String, ///< inline_text (≤ 23 B copy) or view (longer, zero-copy).
};

/// Compile-time canonical name; static storage; "Unknown" for garbage casts.
[[nodiscard]] constexpr std::string_view to_string(SpanAttributeType type) noexcept {
    switch (type) {
        case SpanAttributeType::Int64:  return "Int64";
        case SpanAttributeType::Uint64: return "Uint64";
        case SpanAttributeType::Double: return "Double";
        case SpanAttributeType::Bool:   return "Bool";
        case SpanAttributeType::String: return "String";
        default:                        return "Unknown";
    }
}

/// Parse a canonical tag name; false leaves the out-parameter unchanged.
[[nodiscard]] constexpr bool from_string(std::string_view name, SpanAttributeType& out) noexcept {
    if (name == "Int64")  { out = SpanAttributeType::Int64;  return true; }
    if (name == "Uint64") { out = SpanAttributeType::Uint64; return true; }
    if (name == "Double") { out = SpanAttributeType::Double; return true; }
    if (name == "Bool")   { out = SpanAttributeType::Bool;   return true; }
    if (name == "String") { out = SpanAttributeType::String; return true; }
    return false;
}

/// ADL JSON writer/reader (cold path only; result.hpp get_ptr pattern).
inline void to_json(nlohmann::json& j, const SpanAttributeType type) {
    j = std::string(to_string(type));
}
inline void from_json(const nlohmann::json& j, SpanAttributeType& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

/// Inline bounds (design D5): key and value text capacities.
inline constexpr std::size_t attribute_key_capacity = 15;  ///< chars, + NUL
inline constexpr std::size_t attribute_inline_text_capacity = 23; ///< chars, + length byte

/// Fixed-size trivially copyable tagged-union attribute (design D5 layout,
/// pinned by static_assert at the bottom of this header).
struct alignas(8) SpanAttribute {
    /// The value union: exactly one arm active, selected by `type`.
    union Value {
        std::int64_t i64{};
        std::uint64_t u64;
        double f64;
        bool b;
        struct {
            char text[attribute_inline_text_capacity];
            std::uint8_t length;
        } inline_text;                                  ///< 24 B, always NUL-safe
        struct {
            const char* data;                           ///< caller-owned storage
            std::uint32_t length;                       ///< bytes of text
        } view;                                         ///< zero-copy, 16 B w/ padding

        Value() noexcept : i64{} {} // trivial default: Int64 zero
    };

    char key[attribute_key_capacity + 1]{}; ///< inline bounded key + NUL
    SpanAttributeType type{SpanAttributeType::Int64}; ///< 1 B tag @16
    /// String-arm discriminator @17 (inside the struct's padding bytes 17..23,
    /// invisible to every design-D5 pin: sizeof 48, offsets 0/16/24): true =
    /// inline_text arm, false = zero-copy view arm. The union alone cannot
    /// distinguish them (the view arm leaves the inline-length byte stale).
    bool text_inline{false};
    Value value{};                           ///< 24 B, 8-aligned (offset 24)

    // -- hot-path stores (fixed-offset, no allocation, no lock) -------------

    /// Store a key + int value (plain integer literals land here and widen to
    /// the Int64 arm — without this overload `store("k", 42)` would be
    /// ambiguous between the four numeric arms). Same rejection contract.
    [[nodiscard]] bool store(std::string_view key_, int v) noexcept {
        return store(key_, static_cast<std::int64_t>(v));
    }

    /// Store a key + int64 value; false (attr untouched) when the key is
    /// longer than the inline bound — never truncated.
    [[nodiscard]] bool store(std::string_view key_, std::int64_t v) noexcept {
        if (!key_fits(key_)) { return false; }
        copy_key(key_);
        type = SpanAttributeType::Int64;
        value.i64 = v;
        return true;
    }

    /// Store a key + uint64 value; same rejection contract.
    [[nodiscard]] bool store(std::string_view key_, std::uint64_t v) noexcept {
        if (!key_fits(key_)) { return false; }
        copy_key(key_);
        type = SpanAttributeType::Uint64;
        value.u64 = v;
        return true;
    }

    /// Store a key + double value; same rejection contract.
    [[nodiscard]] bool store(std::string_view key_, double v) noexcept {
        if (!key_fits(key_)) { return false; }
        copy_key(key_);
        type = SpanAttributeType::Double;
        value.f64 = v;
        return true;
    }

    /// Store a key + bool value; same rejection contract.
    [[nodiscard]] bool store(std::string_view key_, bool v) noexcept {
        if (!key_fits(key_)) { return false; }
        copy_key(key_);
        type = SpanAttributeType::Bool;
        value.b = v;
        return true;
    }

    /// Store a key + string value. Values within the inline bound are copied
    /// into inline_text (always safe); longer values become a ZERO-COPY view
    /// whose storage the caller guarantees outlives the span's drain (see the
    /// header contract). Returns false (attr untouched) only for an oversized
    /// KEY; any string VALUE length is representable (inline or view).
    [[nodiscard]] bool store(std::string_view key_, std::string_view v) noexcept {
        if (!key_fits(key_)) { return false; }
        copy_key(key_);
        type = SpanAttributeType::String;
        if (v.size() <= attribute_inline_text_capacity) {
            text_inline = true;
            for (std::size_t i = 0; i < v.size(); ++i) {
                value.inline_text.text[i] = v[i];
            }
            value.inline_text.length = static_cast<std::uint8_t>(v.size());
        } else {
            text_inline = false;
            value.view.data = v.data();
            value.view.length = static_cast<std::uint32_t>(v.size());
        }
        return true;
    }

    /// The stored key as a view (NUL-terminated inline storage).
    [[nodiscard]] std::string_view key_view() const noexcept {
        return std::string_view(key, std::char_traits<char>::length(key));
    }

    /// The stored text value as a view (inline or view arm); meaningful only
    /// for SpanAttributeType::String.
    [[nodiscard]] std::string_view text_view() const noexcept {
        if (type != SpanAttributeType::String) {
            return std::string_view{};
        }
        if (text_inline) {
            return std::string_view(value.inline_text.text, value.inline_text.length);
        }
        return std::string_view(value.view.data, value.view.length);
    }

    /// Cold-path diagnostic (mandate 9); body in src/tracing/record_format.cpp.
    [[nodiscard]] std::string to_string() const;

private:
    [[nodiscard]] static bool key_fits(std::string_view key_) noexcept {
        return key_.size() <= attribute_key_capacity;
    }

    void copy_key(std::string_view key_) noexcept {
        for (std::size_t i = 0; i < key_.size(); ++i) {
            key[i] = key_[i];
        }
        key[key_.size()] = '\0';
    }
};

/// The 320-byte (5 cache lines) span record (design D5 layout, pinned).
struct alignas(64) SpanRecord {
    /// bit0: the span ended through an explicit end() call (vs destruction).
    static constexpr std::uint8_t flag_ended_explicit = 0x1U;

    /// Span name bound: ≤ 47 chars + NUL (bounded copy, longer names truncated).
    static constexpr std::size_t name_capacity = 47;

    TraceId trace_id{};              ///< causal group (root generates fresh).
    SpanId span_id{};                ///< this span.
    SpanId parent_span_id{};         ///< zero = root.
    SpanKind kind{SpanKind::Internal}; ///< causal role.
    SpanStatus status{SpanStatus::Unset}; ///< default Unset (spec).
    std::uint8_t flags{};            ///< flag_ended_explicit etc.
    std::uint8_t attribute_count{};  ///< ≤ MaxAttributes.
    std::uint32_t thread_id{};       ///< cached SYS_gettid.
    chrono::Timestamp start{};       ///< monotonic ns at span start.
    chrono::Timestamp end{};         ///< monotonic ns at span end.
    const char* file_name{};         ///< source_location static storage.
    const char* function_name{};     ///< source_location static storage.
    std::uint32_t line{};            ///< source_location line.
    std::uint32_t column{};          ///< source_location column.
    char name[name_capacity + 1]{};  ///< inline span name + NUL.
    SpanAttribute attributes[MaxAttributes]{}; ///< inline attribute array.

    /// The span name as a view (bounded copy stored inline).
    [[nodiscard]] std::string_view name_view() const noexcept {
        return std::string_view(name, std::char_traits<char>::length(name));
    }

    /// The end-minus-start duration in nanoseconds (monotonic domain).
    [[nodiscard]] std::uint64_t duration_ns() const noexcept {
        return end >= start ? (end - start).nanoseconds() : 0ULL;
    }

    /// Cold-path diagnostic (mandate 9); body in src/tracing/record_format.cpp.
    [[nodiscard]] std::string to_string() const;
};

// --- Layout pins (design D5): a refactor that shifts any field fails the
// build instead of silently changing the record crossing the wire.
static_assert(sizeof(SpanAttribute) == 48, "SpanAttribute must stay 48 B (design D5)");
static_assert(alignof(SpanAttribute) == 8);
static_assert(std::is_trivially_copyable_v<SpanAttribute>);
static_assert(offsetof(SpanAttribute, key) == 0);
static_assert(offsetof(SpanAttribute, type) == 16);
static_assert(offsetof(SpanAttribute, value) == 24);
static_assert(sizeof(SpanRecord) == 320, "SpanRecord must stay 320 B = 5 cache lines");
static_assert(alignof(SpanRecord) == 64, "SpanRecord must stay cache-line aligned");
static_assert(std::is_trivially_copyable_v<SpanRecord>);
static_assert(offsetof(SpanRecord, trace_id) == 0);
static_assert(offsetof(SpanRecord, span_id) == 8);
static_assert(offsetof(SpanRecord, parent_span_id) == 16);
static_assert(offsetof(SpanRecord, kind) == 24);
static_assert(offsetof(SpanRecord, status) == 25);
static_assert(offsetof(SpanRecord, flags) == 26);
static_assert(offsetof(SpanRecord, attribute_count) == 27);
static_assert(offsetof(SpanRecord, thread_id) == 28);
static_assert(offsetof(SpanRecord, start) == 32);
static_assert(offsetof(SpanRecord, end) == 40);
static_assert(offsetof(SpanRecord, file_name) == 48);
static_assert(offsetof(SpanRecord, function_name) == 56);
static_assert(offsetof(SpanRecord, line) == 64);
static_assert(offsetof(SpanRecord, column) == 68);
static_assert(offsetof(SpanRecord, name) == 72);
static_assert(offsetof(SpanRecord, attributes) == 120);
static_assert(320U % 64U == 0, "sizeof must be a whole number of cache lines");

/// Free helpers (mandate 9): mirror the members.
[[nodiscard]] std::string to_string(const SpanAttribute& attribute);
[[nodiscard]] std::string to_string(const SpanRecord& record);

/// ADL JSON (cold path): the attribute as {"key","type","value"}; the record
/// as an object with hex ids, canonical kind/status, ns timestamps, location,
/// name and the typed attribute array. Non-throwing readers; wrong-typed
/// input leaves values unchanged. Bodies in src/tracing/record_format.cpp.
void to_json(nlohmann::json& j, const SpanAttribute& attribute);
void from_json(const nlohmann::json& j, SpanAttribute& out) noexcept;
void to_json(nlohmann::json& j, const SpanRecord& record);
void from_json(const nlohmann::json& j, SpanRecord& out) noexcept;

} // namespace nexigen::tracing

/// The tag enum formats as its canonical name (mandate 11).
template <>
struct std::formatter<nexigen::tracing::SpanAttributeType> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::tracing::SpanAttributeType type, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::tracing::to_string(type), ctx);
    }
};

/// Attributes format as their diagnostic text (mandate 11).
template <>
struct std::formatter<nexigen::tracing::SpanAttribute> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::SpanAttribute& attribute, Ctx& ctx) const {
        const std::string text = attribute.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

/// Records format as their diagnostic text (mandate 11).
template <>
struct std::formatter<nexigen::tracing::SpanRecord> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::SpanRecord& record, Ctx& ctx) const {
        const std::string text = record.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
