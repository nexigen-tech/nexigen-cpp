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

#include <array>
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

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::tracing {

/// @brief Inline attribute capacity per span (request-mandated spelling, design D3).
///
/// A span carries at most four attributes; a fifth `set_attribute` is rejected
/// without side effects. The bound sizes `SpanRecord::attributes` and therefore
/// the pinned 320-byte record.
///
/// @note Spec `tracing/span-context`, requirement "Span attribute capacity bounds".
inline constexpr std::size_t MaxAttributes = 4;

/// @brief Type tag of a `SpanAttribute` value (enum contract of mandates 9–11).
///
/// Selects which arm of `SpanAttribute::value` is live; the string arm is
/// further split into inline text versus zero-copy view by
/// `SpanAttribute::text_inline`. Values are append-only and, with `String`
/// last, the tag fits the 1-byte field at offset 16.
enum class SpanAttributeType : std::uint8_t {
    Int64,  ///< `std::int64_t` payload.
    Uint64, ///< `std::uint64_t` payload.
    Double, ///< `double` payload (bit-for-bit through copies/JSON).
    Bool,   ///< `bool` payload.
    String, ///< `inline_text` (≤ 23 B copy) or `view` (longer, zero-copy).
};

} // namespace nexigen::tracing

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table mapping every `SpanAttributeType` to its canonical name.
///
/// The shared enum engine backs `to_string`/`from_string` and the attribute JSON
/// surface with one spelling set. Static constexpr storage; cold path only.
///
/// @note Spec `tracing/span-enums`, requirement "Constexpr span enum names".
template <> struct EnumTraits<nexigen::tracing::SpanAttributeType> {
    /// @brief One table row: a tag and the exact text it renders as.
    struct Entry {
        nexigen::tracing::SpanAttributeType value; ///< Tag described by the row.
        std::string_view name;                     ///< Canonical, case-sensitive spelling.
    };
    /// @brief One row per tag, in declaration order (append-only).
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::tracing::SpanAttributeType::Int64, "Int64"},
        {nexigen::tracing::SpanAttributeType::Uint64, "Uint64"},
        {nexigen::tracing::SpanAttributeType::Double, "Double"},
        {nexigen::tracing::SpanAttributeType::Bool, "Bool"},
        {nexigen::tracing::SpanAttributeType::String, "String"},
    }};
};

} // namespace nexigen::core

namespace nexigen::tracing {

/// @brief Compile-time canonical name; static storage; "Unknown" for garbage casts.
///
/// @param type Tag to render.
/// @return Exact tag spelling (`"Int64"`, `"String"`, ...).
/// @note Spec `tracing/span-enums`, requirement "Constexpr span enum names".
[[nodiscard]] constexpr std::string_view to_string(SpanAttributeType type) noexcept {
    return core::enum_name(type);
}

/// @brief Parse a canonical tag name; false leaves the out-parameter unchanged.
///
/// @param name Exact, case-sensitive tag spelling.
/// @param out Destination updated only on a successful match.
/// @return `true` when the name matched a tag; `false` otherwise.
[[nodiscard]] constexpr bool from_string(std::string_view name, SpanAttributeType& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for the attribute tag (cold path only).
///
/// @param j Destination document or field to overwrite.
/// @param type Tag to serialize as its canonical-name string.
/// @note Spec `tracing/span-enums`, requirement "Non-throwing span enum JSON
///       serialization"; result.hpp get_ptr pattern.
inline void to_json(nlohmann::json& j, const SpanAttributeType type) {
    core::mandate::enum_to_json(j, type);
}

/// @brief ADL JSON reader for the attribute tag (cold path only).
///
/// @param j JSON value to read.
/// @param out Destination left unchanged for non-string or unknown input.
/// @note Spec `tracing/span-enums`, requirement "Non-throwing span enum JSON
///       serialization"; never throws under `JSON_NOEXCEPTION=1`.
inline void from_json(const nlohmann::json& j, SpanAttributeType& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

/// @brief Inline bounds (design D5): key and value text capacities.
///
/// A longer key is rejected whole; a longer string value is stored as a
/// zero-copy view instead of being truncated.
/// @note Spec `tracing/span-context`, requirement "Span attribute capacity bounds".
inline constexpr std::size_t attribute_key_capacity = 15;         ///< chars, + NUL
inline constexpr std::size_t attribute_inline_text_capacity = 23; ///< chars, + length byte

/// @brief Fixed-size trivially copyable tagged-union attribute (design D5 layout,
///        pinned by static_assert at the bottom of this header).
///
/// Exactly 48 bytes with a 16-byte inline key, a 1-byte type tag and a 24-byte
/// value union. Storage is fixed-offset: `store` never allocates, never locks
/// and never grows the record, which is what keeps it usable on the span hot
/// path. String values ≤ 23 bytes are copied into `inline_text`; longer values
/// become a zero-copy `view` over caller-owned storage that must outlive the
/// span's drain (string literals and program-static tables qualify).
///
/// @note Spec `tracing/span-context`, requirement "Fixed-size span attribute union".
/// @note Spec `tracing/span-context`, requirement "Span attribute string storage".
struct alignas(8) SpanAttribute {
    /// @brief The value union: exactly one arm active, selected by `type`.
    ///
    /// The `String` tag needs the extra `text_inline` discriminator because
    /// the union alone cannot tell the two string arms apart.
    union Value {
        std::int64_t i64{}; ///< Payload for `SpanAttributeType::Int64`.
        std::uint64_t u64;  ///< Payload for `SpanAttributeType::Uint64`.
        double f64;         ///< Payload for `SpanAttributeType::Double`.
        bool b;             ///< Payload for `SpanAttributeType::Bool`.
        struct {
            char text[attribute_inline_text_capacity]; ///< Copied text bytes.
            std::uint8_t length;                       ///< Valid byte count.
        } inline_text;                                 ///< 24 B, always NUL-safe
        struct {
            const char* data;     ///< caller-owned storage
            std::uint32_t length; ///< bytes of text
        } view;                   ///< zero-copy, 16 B w/ padding

        /// @brief Trivial default: leave the union in its `Int64` zero state.
        Value() noexcept : i64{} {}
    };

    char key[attribute_key_capacity + 1]{};           ///< inline bounded key + NUL
    SpanAttributeType type{SpanAttributeType::Int64}; ///< 1 B tag @16
    /// String-arm discriminator @17 (inside the struct's padding bytes 17..23,
    /// invisible to every design-D5 pin: sizeof 48, offsets 0/16/24): true =
    /// inline_text arm, false = zero-copy view arm. The union alone cannot
    /// distinguish them (the view arm leaves the inline-length byte stale).
    bool text_inline{false};
    Value value{}; ///< 24 B, 8-aligned (offset 24)

    // -- hot-path stores (fixed-offset, no allocation, no lock) -------------

    /// @brief Store a key + int value.
    ///
    /// Plain integer literals land here and widen to the `Int64` arm — without
    /// this overload `store("k", 42)` would be ambiguous between the four
    /// numeric arms.
    ///
    /// @param key_ Attribute key; must fit `attribute_key_capacity`.
    /// @param v Value to store.
    /// @return `true` when stored; `false` for an oversized key with the
    ///         attribute untouched.
    /// @note Spec `tracing/span-context`, requirement "Span attribute capacity bounds".
    [[nodiscard]] bool store(std::string_view key_, int v) noexcept {
        return store(key_, static_cast<std::int64_t>(v));
    }

    /// @brief Store a key + int64 value.
    ///
    /// @param key_ Attribute key; rejected whole when longer than the inline bound.
    /// @param v Value to store.
    /// @return `true` when stored; `false` (attribute untouched) for an
    ///         oversized key — the key is never truncated.
    /// @note Spec `tracing/span-context`, requirement "Span attribute capacity bounds".
    [[nodiscard]] bool store(std::string_view key_, std::int64_t v) noexcept {
        if (!key_fits(key_)) {
            return false;
        }
        copy_key(key_);
        type = SpanAttributeType::Int64;
        value.i64 = v;
        return true;
    }

    /// @brief Store a key + uint64 value.
    ///
    /// @param key_ Attribute key; rejected whole when longer than the inline bound.
    /// @param v Value to store.
    /// @return `true` when stored; `false` with the attribute untouched otherwise.
    [[nodiscard]] bool store(std::string_view key_, std::uint64_t v) noexcept {
        if (!key_fits(key_)) {
            return false;
        }
        copy_key(key_);
        type = SpanAttributeType::Uint64;
        value.u64 = v;
        return true;
    }

    /// @brief Store a key + double value; bit-for-bit preserved.
    ///
    /// @param key_ Attribute key; rejected whole when longer than the inline bound.
    /// @param v Value to store.
    /// @return `true` when stored; `false` with the attribute untouched otherwise.
    [[nodiscard]] bool store(std::string_view key_, double v) noexcept {
        if (!key_fits(key_)) {
            return false;
        }
        copy_key(key_);
        type = SpanAttributeType::Double;
        value.f64 = v;
        return true;
    }

    /// @brief Store a key + bool value.
    ///
    /// @param key_ Attribute key; rejected whole when longer than the inline bound.
    /// @param v Value to store.
    /// @return `true` when stored; `false` with the attribute untouched otherwise.
    [[nodiscard]] bool store(std::string_view key_, bool v) noexcept {
        if (!key_fits(key_)) {
            return false;
        }
        copy_key(key_);
        type = SpanAttributeType::Bool;
        value.b = v;
        return true;
    }

    /// @brief Store a key + string value.
    ///
    /// Values within the inline bound are copied into `inline_text` (always
    /// safe); longer values become a ZERO-COPY view whose storage the caller
    /// guarantees outlives the span's drain (see the header contract).
    ///
    /// @param key_ Attribute key; rejected whole when longer than the inline bound.
    /// @param v Value text; any length is representable (inline copy or view).
    /// @return `false` (attribute untouched) only for an oversized KEY.
    /// @note Spec `tracing/span-context`, requirement "Span attribute string storage".
    /// @warning A view value must point at string-literal or program-static
    ///          storage; a temporary buffer dangles by drain time.
    [[nodiscard]] bool store(std::string_view key_, std::string_view v) noexcept {
        if (!key_fits(key_)) {
            return false;
        }
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

    /// @brief The stored key as a view (NUL-terminated inline storage).
    ///
    /// @return View over `key`, without the terminator; empty for a default
    ///         attribute.
    [[nodiscard]] std::string_view key_view() const noexcept {
        return std::string_view(key, std::char_traits<char>::length(key));
    }

    /// @brief The stored text value as a view (inline or view arm).
    ///
    /// @return The string payload; an empty view when the tag is not
    ///         `SpanAttributeType::String`.
    [[nodiscard]] std::string_view text_view() const noexcept {
        if (type != SpanAttributeType::String) {
            return std::string_view{};
        }
        if (text_inline) {
            return std::string_view(value.inline_text.text, value.inline_text.length);
        }
        return std::string_view(value.view.data, value.view.length);
    }

    /// @brief Cold-path diagnostic (mandate 9).
    ///
    /// @return `key=value` style text for logs, diagnostics and tests.
    /// @note Allocates; never called from the span hot path. Body in
    ///       `src/tracing/record_format.cpp`.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Whether `key_` fits the inline key bound (NUL excluded).
    ///
    /// @param key_ Candidate key.
    /// @return `true` when `key_.size() <= attribute_key_capacity`.
    [[nodiscard]] static bool key_fits(std::string_view key_) noexcept {
        return key_.size() <= attribute_key_capacity;
    }

    /// @brief Copy `key_` into the inline key buffer and NUL-terminate it.
    ///
    /// @param key_ Key already validated by `key_fits`.
    void copy_key(std::string_view key_) noexcept {
        for (std::size_t i = 0; i < key_.size(); ++i) {
            key[i] = key_[i];
        }
        key[key_.size()] = '\0';
    }
};

/// @brief The 320-byte (5 cache lines) span record (design D5 layout, pinned).
///
/// The one value that crosses the producer/consumer boundary: the owner thread
/// fills it with fixed-offset stores through `ScopedSpan`, the SPSC shard
/// carries it by copy, and the sink renders it cold-side. Trivially copyable
/// and `alignas(64)`, so a shard slot never straddles a cache line.
///
/// @note Spec `tracing/span-context`, requirement "Cache-line-aligned span record".
struct alignas(64) SpanRecord {
    /// @brief bit0: the span ended through an explicit end() call (vs destruction).
    static constexpr std::uint8_t flag_ended_explicit = 0x1U;

    /// @brief Span name bound: ≤ 47 chars + NUL (bounded copy, longer names truncated).
    static constexpr std::size_t name_capacity = 47;

    TraceId trace_id{};                        ///< causal group (root generates fresh).
    SpanId span_id{};                          ///< this span.
    SpanId parent_span_id{};                   ///< zero = root.
    SpanKind kind{SpanKind::Internal};         ///< causal role.
    SpanStatus status{SpanStatus::Unset};      ///< default Unset (spec).
    std::uint8_t flags{};                      ///< flag_ended_explicit etc.
    std::uint8_t attribute_count{};            ///< ≤ MaxAttributes.
    std::uint32_t thread_id{};                 ///< cached SYS_gettid.
    chrono::Timestamp start{};                 ///< monotonic ns at span start.
    chrono::Timestamp end{};                   ///< monotonic ns at span end.
    const char* file_name{};                   ///< source_location static storage.
    const char* function_name{};               ///< source_location static storage.
    std::uint32_t line{};                      ///< source_location line.
    std::uint32_t column{};                    ///< source_location column.
    char name[name_capacity + 1]{};            ///< inline span name + NUL.
    SpanAttribute attributes[MaxAttributes]{}; ///< inline attribute array.

    /// @brief The span name as a view (bounded copy stored inline).
    ///
    /// @return View over `name`, without the NUL; never allocates.
    [[nodiscard]] std::string_view name_view() const noexcept {
        return std::string_view(name, std::char_traits<char>::length(name));
    }

    /// @brief The end-minus-start duration in nanoseconds (monotonic domain).
    ///
    /// @return Elapsed nanoseconds, or `0` for an inverted/malformed pair
    ///         (`end < start`).
    [[nodiscard]] std::uint64_t duration_ns() const noexcept {
        return end >= start ? (end - start).nanoseconds() : 0ULL;
    }

    /// @brief Cold-path diagnostic (mandate 9).
    ///
    /// @return Rendered record text for diagnostics and tests.
    /// @note Allocates; the producer never calls it. Body in
    ///       `src/tracing/record_format.cpp`.
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

/// @brief Free helper (mandate 9): mirrors `SpanAttribute::to_string`.
///
/// @param attribute Attribute to render.
/// @return The same text as the member.
[[nodiscard]] std::string to_string(const SpanAttribute& attribute);

/// @brief Free helper (mandate 9): mirrors `SpanRecord::to_string`.
///
/// @param record Record to render.
/// @return The same text as the member.
[[nodiscard]] std::string to_string(const SpanRecord& record);

/// @brief ADL JSON writer for an attribute: `{"key","type","value"}` (cold path).
///
/// @param j Destination document or field to overwrite.
/// @param attribute Attribute to serialize; string values render their text.
/// @note Spec `tracing/record-format`, requirement "SpanAttribute deserialization
///       is all-or-nothing".
void to_json(nlohmann::json& j, const SpanAttribute& attribute);

/// @brief Whole-attribute JSON reader; rejects on any mismatch.
///
/// @param j JSON object to read.
/// @param out Destination left unchanged when the key is missing/oversized,
///        the tag is unknown, or the value type does not match the tag.
/// @note Spec `tracing/record-format`, requirement "SpanAttribute deserialization
///       is all-or-nothing".
void from_json(const nlohmann::json& j, SpanAttribute& out) noexcept;

/// @brief ADL JSON writer for a record: hex ids, canonical kind/status, ns
///        timestamps, location, name and the typed attribute array.
///
/// @param j Destination document or field to overwrite.
/// @param record Record to serialize.
/// @note Spec `tracing/record-format`, requirement "SpanRecord deserialization
///       delegates to substructure helpers". Cold path only.
void to_json(nlohmann::json& j, const SpanRecord& record);

/// @brief Non-throwing record JSON reader composed of substructure helpers.
///
/// @param j JSON value to read.
/// @param out Destination; missing or wrong-typed fields keep their previous
///        values and are never partially applied.
/// @note Spec `tracing/record-format`, requirement "SpanRecord deserialization
///       delegates to substructure helpers".
void from_json(const nlohmann::json& j, SpanRecord& out) noexcept;

} // namespace nexigen::tracing

/// The tag enum formats as its canonical name (mandate 11).

/// @brief `std::formatter` for `SpanAttribute` (mandate 11).
///
/// Renders exactly `SpanAttribute::to_string()` through the
/// `std::string_view` formatter, so fill/width/alignment options still apply.
template <>
struct std::formatter<nexigen::tracing::SpanAttribute> : std::formatter<std::string_view> {
    /// @brief Render one attribute as its diagnostic text.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param attribute Attribute to render.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::SpanAttribute& attribute, Ctx& ctx) const {
        const std::string text = attribute.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

/// @brief `std::formatter` for `SpanRecord` (mandate 11).
///
/// Renders exactly `SpanRecord::to_string()`; diagnostics only, never reached
/// from a producer or a ring drain.
template <> struct std::formatter<nexigen::tracing::SpanRecord> : std::formatter<std::string_view> {
    /// @brief Render one record as its diagnostic text.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param record Record to render.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::tracing::SpanRecord& record, Ctx& ctx) const {
        const std::string text = record.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
