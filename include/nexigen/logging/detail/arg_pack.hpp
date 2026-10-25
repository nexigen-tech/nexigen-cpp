// nexigen::logging — compile-time argument packing for deferred formatting
// (change add-serialization-and-wait-free-logging, design D5; spec
// logging/log-record "Deferred type-erased argument formatting").
//
// The producer never renders: it serializes each call's arguments into the
// record's fixed inline blob at compile-time offsets and stores a template-
/// instantiated render callback. The consumer rehydrates the values and runs
// std::vformat_to into caller-owned scratch — allocation-free in both passes.
//
// LAYOUT (MessageCapacity = M, InlineStringCap = M/2):
//   - every non-string argument occupies one 8-byte slot (memcpy in/out: no
//     alignment or lifetime UB, values are trivially copyable by concept);
//   - every std::string_view occupies a fixed 2 + InlineStringCap slot:
//     { uint16 length (host order, both ends same process); inline bytes },
//     truncating the COPY (never the caller's data) and flagging bit0;
//   - slots are laid out in argument order; offsets are pure functions of
//     the pack, identical for packer and renderer.
//
// The fixed layout means the total blob size is a compile-time constant:
// static_assert in the emit path rejects packs that would not fit BEFORE
// any runtime check, so the truncation flag only ever describes string
// content, never a corrupted layout.
//
// HOT PATH: pack is one memcpy per argument at constant offsets — no loop
// above the expansion, no allocation, no formatting.
//
// ENDIANNESS: blobs are produced and consumed in one process on one machine;
// raw memcpy round-trips are exact.
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <iterator>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <nexigen/chrono/timestamp.hpp>

namespace nexigen::logging::detail {

/// Flags stored in LogRecord::flags (kept here so packer and sinks agree).
inline constexpr std::uint8_t flag_args_truncated = 1U;  ///< bit0: producer
inline constexpr std::uint8_t flag_render_truncated = 2U; ///< bit1: consumer

/// What may be serialized into a record's inline argument blob: trivially
/// copyable scalars/enums, the chrono timestamp, std::string_view, and the
/// pointer types std::formatter understands. Anything else fails the call
/// at compile time (spec: static_assert at the call site, no runtime path).
template <class T>
concept Packable = std::is_trivially_copyable_v<T> && (
    std::is_arithmetic_v<T> || std::is_enum_v<T> ||
    std::same_as<T, std::string_view> ||
    std::same_as<T, nexigen::chrono::Timestamp> ||
    std::same_as<T, const char*> ||
    std::same_as<T, void*> ||
    std::same_as<T, const void*>);

/// Bytes a std::string_view slot reserves: length prefix + inline copy.
constexpr std::size_t string_slot_size(std::size_t message_capacity) noexcept {
    return 2U + message_capacity / 2U;
}

/// Fixed byte width of one argument's slot.
template <class T, std::size_t MessageCapacity>
[[nodiscard]] constexpr std::size_t slot_size() noexcept {
    return std::same_as<T, std::string_view> ? string_slot_size(MessageCapacity) : 8U;
}

/// Compile-time offset table for a pack (argument order, cumulative).
template <std::size_t MessageCapacity, class... Args>
[[nodiscard]] constexpr std::array<std::size_t, sizeof...(Args)> arg_offsets() noexcept {
    std::array<std::size_t, sizeof...(Args)> offsets{};
    [[maybe_unused]] std::size_t cursor = 0;
    std::size_t index = 0;
    ((offsets[index++] = cursor, cursor += slot_size<Args, MessageCapacity>()), ...);
    return offsets;
}

/// Total blob bytes a pack needs (fixed per argument-type pack).
template <std::size_t MessageCapacity, class... Args>
[[nodiscard]] constexpr std::size_t arg_total() noexcept {
    return (slot_size<Args, MessageCapacity>() + ... + 0U);
}

/// Serialize one argument into its slot; string copies truncate (flagged).
template <std::size_t MessageCapacity, class T>
void pack_one(char* blob, std::size_t offset, const T& value, std::uint8_t& flags) noexcept {
    if constexpr (std::same_as<std::remove_cvref_t<T>, std::string_view>) {
        constexpr std::size_t cap = string_slot_size(MessageCapacity) - 2U;
        const std::size_t wanted = value.size();
        const std::size_t take = wanted < cap ? wanted : cap;
        const auto len = static_cast<std::uint16_t>(take);
        std::memcpy(blob + offset, &len, sizeof(len));
        if (take > 0U) {
            std::memcpy(blob + offset + 2U, value.data(), take);
        }
        if (wanted > cap) {
            flags |= flag_args_truncated;
        }
    } else {
        std::memcpy(blob + offset, &value, sizeof(std::remove_cvref_t<T>));
    }
}

/// Rehydrate one argument from its slot (strings view the record's storage).
template <std::size_t MessageCapacity, class T>
[[nodiscard]] T unpack_one(const char* blob, std::size_t offset) noexcept {
    if constexpr (std::same_as<T, std::string_view>) {
        std::uint16_t len = 0U;
        std::memcpy(&len, blob + offset, sizeof(len));
        return std::string_view(blob + offset + 2U, len);
    } else {
        T value{};
        std::memcpy(&value, blob + offset, sizeof(value));
        return value;
    }
}

/// Bounded output adapter for std::vformat_to (the checked/bounded overload
/// needs a compile-time format_string; we hold a runtime pointer, so writes
/// flow through this sink: bytes up to `left` land in the caller buffer, the
/// rest are discarded into a one-byte spill. Never allocates.)
struct Bounded_sink {
    using value_type        = char;
    using difference_type   = std::ptrdiff_t;
    using reference         = char&;
    using iterator_category = std::output_iterator_tag;

    char* p{nullptr};
    std::size_t left{0};
    char spill{};

    [[nodiscard]] char& operator*() noexcept { return left > 0U ? *p : spill; }
    Bounded_sink& operator++() noexcept {
        if (left > 0U) { ++p; --left; }
        return *this;
    }
    Bounded_sink operator++(int) noexcept {
        const Bounded_sink copy = *this;
        ++*this;
        return copy;
    }
};

/// Render a packed tuple through the record's stored callback. The format
/// string was type-checked at the call site (std::format_string), so the
/// runtime parse here cannot fail; the bounded sink never writes past `cap`
/// (truncation is the caller's to flag). std::vformat_to on a GNU unchecked
/// string_view overload keeps the whole path allocation-free.
template <std::size_t MessageCapacity, class... Args, std::size_t... I>
[[nodiscard]] std::size_t render_tuple(std::index_sequence<I...>, const char* fmt,
                                       [[maybe_unused]] const char* blob,
                                       char* out, std::size_t cap) {
    [[maybe_unused]] constexpr auto offsets = arg_offsets<MessageCapacity, Args...>();
    std::tuple<Args...> values{unpack_one<MessageCapacity, Args>(blob, offsets[I])...};
    return std::apply(
        [&](Args&... live) noexcept -> std::size_t {
            const auto fargs = std::make_format_args(live...);
            Bounded_sink sink{.p = out, .left = cap};
            const Bounded_sink end = std::vformat_to(sink, std::string_view(fmt), fargs);
            return static_cast<std::size_t>(end.p - out);
        },
        values);
}

/// The per-pack render callback stored in LogRecord::render (signature must
/// match detail::render_fn). Args here are already cvref-stripped slot types.
template <std::size_t MessageCapacity, class... Args>
[[nodiscard]] std::size_t render_impl(const char* fmt, const char* blob, std::uint16_t,
                                      char* out, std::size_t cap) noexcept {
    return render_tuple<MessageCapacity, Args...>(
        std::make_index_sequence<sizeof...(Args)>{}, fmt, blob, out, cap);
}

/// Renderer for synthesized (no-argument) records: the blob IS the message
/// text, copied verbatim. Used by the drop-summary path, whose text is built
/// once on a window transition and must survive in record-owned storage.
[[nodiscard]] inline std::size_t render_text(const char*, const char* blob, std::uint16_t len,
                                             char* out, std::size_t cap) noexcept {
    const std::size_t n = len < cap ? len : cap;
    if (n > 0U) {
        std::memcpy(out, blob, n);
    }
    return n;
}

} // namespace nexigen::logging::detail
