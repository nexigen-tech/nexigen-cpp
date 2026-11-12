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
#include <nexigen/logging/detail/bounded_sink.hpp>

namespace nexigen::logging::detail {

/// @brief Truncation flag for string arguments, kept where packer and sinks agree.
///
/// bit0 of `LogRecord::flags`, raised by `pack_one` on the producer thread when
/// an inline `std::string_view` copy did not fit its slot. It describes string
/// content only: the slot layout itself is a compile-time constant, so a flag
/// can never mean a corrupted pack.
///
/// @note Spec `logging/log-record`, requirement "Fixed-size trivially-copyable
///       record".
inline constexpr std::uint8_t flag_args_truncated = 1U; ///< bit0: producer
/// @brief Render-truncation flag for the rendered message, set by the consumer.
///
/// bit1 of `LogRecord::flags`, raised by `LogRecord::render_into` when the
/// rendered text consumed the whole output buffer.
///
/// @note Spec `logging/log-record`, requirement "Consumer-side rendering of
///       packed records".
inline constexpr std::uint8_t flag_render_truncated = 2U; ///< bit1: consumer

/// @brief Constraint on argument types that may be serialized into a record.
///
/// Accepts trivially copyable arithmetic and enum values, the monotonic
/// `nexigen::chrono::Timestamp`, `std::string_view`, and the pointer types
/// `std::formatter` understands (`const char*`, `void*`, `const void*`).
/// Anything else fails the variadic call at compile time through the
/// `requires` clause on `Producer::emit`; there is no runtime rejection path.
///
/// @tparam T Candidate argument type, already cvref-stripped by the caller.
/// @note Spec `logging/log-record`, requirement "Deferred type-erased argument
///       formatting".
template <class T>
concept Packable =
    std::is_trivially_copyable_v<T> &&
    (std::is_arithmetic_v<T> || std::is_enum_v<T> || std::same_as<T, std::string_view> ||
     std::same_as<T, nexigen::chrono::Timestamp> || std::same_as<T, const char*> ||
     std::same_as<T, void*> || std::same_as<T, const void*>);

/// @brief Bytes reserved for one `std::string_view` argument slot.
///
/// The slot is a `uint16` length prefix (host order; producer and consumer are
/// the same process) followed by an inline copy of at most
/// `message_capacity / 2` bytes.
///
/// @param message_capacity Record message capacity (`MessageCapacity`).
/// @return Slot width in bytes, `2 + message_capacity / 2`.
constexpr std::size_t string_slot_size(std::size_t message_capacity) noexcept {
    return 2U + message_capacity / 2U;
}

/// @brief Fixed byte width of one argument slot.
///
/// Every non-string argument occupies one 8-byte slot (`memcpy` in and out, so
/// alignment and lifetime are irrelevant for trivially copyable values); a
/// `std::string_view` occupies a `string_slot_size()` slot.
///
/// @tparam T Slot value type, already cvref-stripped.
/// @tparam MessageCapacity Record message capacity (`MessageCapacity`).
/// @return `8` for scalars, or the string slot width for `std::string_view`.
template <class T, std::size_t MessageCapacity>
[[nodiscard]] constexpr std::size_t slot_size() noexcept {
    return std::same_as<T, std::string_view> ? string_slot_size(MessageCapacity) : 8U;
}

/// @brief Compile-time offset table for one argument pack.
///
/// Slots are laid out in argument order with cumulative offsets, making the
/// positions a pure function of `(MessageCapacity, Args...)`; the packer and the
/// renderer derive identical offsets from the same type pack.
///
/// @tparam MessageCapacity Record message capacity (`MessageCapacity`).
/// @tparam Args Pack value types in call order, already cvref-stripped.
/// @return Array of slot offsets, index-aligned with `Args`.
template <std::size_t MessageCapacity, class... Args>
[[nodiscard]] constexpr std::array<std::size_t, sizeof...(Args)> arg_offsets() noexcept {
    std::array<std::size_t, sizeof...(Args)> offsets{};
    [[maybe_unused]] std::size_t cursor = 0;
    std::size_t index = 0;
    ((offsets[index++] = cursor, cursor += slot_size<Args, MessageCapacity>()), ...);
    return offsets;
}

/// @brief Total inline blob bytes one argument pack needs.
///
/// @tparam MessageCapacity Record message capacity (`MessageCapacity`).
/// @tparam Args Pack value types in call order, already cvref-stripped.
/// @return Sum of every slot width; `Producer::emit` `static_assert`s that it
///         fits `MessageCapacity` before any runtime check.
template <std::size_t MessageCapacity, class... Args>
[[nodiscard]] constexpr std::size_t arg_total() noexcept {
    return (slot_size<Args, MessageCapacity>() + ... + 0U);
}

/// @brief Serialize one argument into its pre-computed slot.
///
/// Scalars are `memcpy`-copied. A `std::string_view` is copied behind its
/// length prefix, truncating the COPY (never the caller's data) and raising
/// `flag_args_truncated` when it does not fit.
///
/// @tparam MessageCapacity Record message capacity (`MessageCapacity`).
/// @tparam T Argument type as passed by the caller (may carry references).
/// @param blob Record-owned inline argument blob.
/// @param offset Byte offset of this argument's slot.
/// @param value Argument value to serialize.
/// @param flags Record flags word; bit0 is raised on string truncation.
/// @note Spec `logging/log-record`, requirement "Fixed-size trivially-copyable
///       record"; wait-free and allocation-free.
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

/// @brief Rehydrate one argument from its slot.
///
/// Called on the consumer thread only. A `std::string_view` result views the
/// record's own blob, so it remains valid as long as the record does, never as
/// long as the original caller's buffer.
///
/// @tparam MessageCapacity Record message capacity (`MessageCapacity`).
/// @tparam T Slot value type, already cvref-stripped.
/// @param blob Record-owned inline argument blob.
/// @param offset Byte offset of this argument's slot.
/// @return Value copied out of the slot, or a view into the record's blob.
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

/// @brief Render a packed tuple through the record's stored callback core.
///
/// Unpacks every argument, builds `std::format_args` over the live values, and
/// runs `std::vformat_to` into `out` through `BoundedSink`. The format string
/// was type-checked at the call site (`std::format_string`), so the runtime
/// parse here cannot fail; the bounded sink never writes past `cap`
/// (truncation is the caller's to flag). The GNU unchecked `string_view`
/// overload keeps the whole path allocation-free.
///
/// @tparam MessageCapacity Record message capacity (`MessageCapacity`).
/// @tparam Args Pack value types, already cvref-stripped.
/// @tparam I Index sequence covering `Args` (passed as the first argument).
/// @param fmt NUL-terminated format string in program-static storage.
/// @param blob Record-owned inline argument blob.
/// @param out Caller-owned render scratch.
/// @param cap Capacity of `out` in bytes.
/// @return Bytes actually written, at most `cap`.
/// @note Spec `logging/log-record`, requirement "Consumer-side rendering of
///       packed records".
template <std::size_t MessageCapacity, class... Args, std::size_t... I>
[[nodiscard]] std::size_t render_tuple(std::index_sequence<I...>, const char* fmt,
                                       [[maybe_unused]] const char* blob, char* out,
                                       std::size_t cap) {
    [[maybe_unused]] constexpr auto offsets = arg_offsets<MessageCapacity, Args...>();
    std::tuple<Args...> values{unpack_one<MessageCapacity, Args>(blob, offsets[I])...};
    return std::apply(
        [&](Args&... live) noexcept -> std::size_t {
            const auto fargs = std::make_format_args(live...);
            BoundedSink sink{.p = out, .left = cap};
            const BoundedSink end = std::vformat_to(sink, std::string_view(fmt), fargs);
            return static_cast<std::size_t>(end.p - out);
        },
        values);
}

/// @brief Type-erased render callback bound to one argument pack.
///
/// Matches `nexigen::logging::record_render_fn`, the address stored in
/// `LogRecord::render` by `Producer::emit`. `Args` here are the cvref-stripped
/// slot types; the unnamed `uint16` argument carries the blob length but is
/// unused, because the layout is a compile-time function of `MessageCapacity`
/// and `Args`.
///
/// @tparam MessageCapacity Record message capacity (`MessageCapacity`).
/// @tparam Args Pack value types, already cvref-stripped.
/// @param fmt NUL-terminated format string in program-static storage.
/// @param blob Record-owned inline argument blob.
/// @param out Caller-owned render scratch.
/// @param cap Capacity of `out` in bytes.
/// @return Bytes written into `out`, at most `cap`.
/// @note Spec `logging/log-record`, requirement "Deferred type-erased argument
///       formatting" (producer never renders) and "Consumer-side rendering of
///       packed records" (this callback runs on the consumer only).
template <std::size_t MessageCapacity, class... Args>
[[nodiscard]] std::size_t render_impl(const char* fmt, const char* blob, std::uint16_t, char* out,
                                      std::size_t cap) noexcept {
    return render_tuple<MessageCapacity, Args...>(std::make_index_sequence<sizeof...(Args)>{}, fmt,
                                                  blob, out, cap);
}

/// @brief Render a synthesized no-argument record whose blob is the message.
///
/// Copies the record's stored text verbatim, capped at `cap`. Used by the
/// drop-summary path, whose text is built once on a window transition and must
/// survive in record-owned storage.
///
/// The unnamed first argument is an unused format pointer (synthesized records
/// store `""`).
///
/// @param blob Record-owned text bytes.
/// @param len Number of valid bytes in `blob`.
/// @param out Caller-owned render scratch.
/// @param cap Capacity of `out` in bytes.
/// @return Bytes copied, at most `cap`.
/// @note Spec `logging/multi-producer`, requirement "Drop summaries are
///       synthesized by the worker".
[[nodiscard]] inline std::size_t render_text(const char*, const char* blob, std::uint16_t len,
                                             char* out, std::size_t cap) noexcept {
    const std::size_t n = len < cap ? len : cap;
    if (n > 0U) {
        std::memcpy(out, blob, n);
    }
    return n;
}

} // namespace nexigen::logging::detail
