// nexigen::tracing — the fixed cross-process span record (change
// add-iceoryx2-trace-sink; spec tracing/iceoryx2-trace-sink "SpanFrame is a
// fixed pointer-free wire record"; design D1).
//
// SpanFrame mirrors SpanRecord's value fields but cannot carry the two
// program-static source pointers: they are invalid in another process. File
// and function are therefore stable 64-bit FNV-1a hashes, and the publisher
// identity (application code, pid, sequence, publish timestamp) plus the wire
// format version live inside the frame, matching LogFrame/MetricFrame. No
// pointer crosses shared memory.
//
// Pinned layout (design D1), pinned by static_assert:
//   0   TraceId        trace_id          8 B
//   8   SpanId         span_id           8 B
//  16   SpanId         parent_span_id    8 B (zero = root)
//  24   SpanKind       kind              1 B
//  25   SpanStatus     status            1 B
//  26   uint8_t        flags             1 B
//  27   uint8_t        attribute_count   1 B (≤ MaxAttributes = 4)
//  28   uint32_t       thread_id         4 B
//  32   uint64_t       start_ns          8 B (monotonic)
//  40   uint64_t       end_ns            8 B (monotonic)
//  48   uint64_t       file_hash         8 B (FNV-1a of the static path)
//  56   uint64_t       function_hash     8 B (FNV-1a of the static name)
//  64   uint32_t       line              4 B
//  68   uint32_t       column            4 B
//  72   char           name[48]         48 B (≤ 47 chars + NUL)
// 120   SpanAttribute  attributes[4]   192 B (4 × 48)
// 312   char           app_id[4]         4 B (ASCII code, NUL-trimmed view)
// 316   uint32_t       pid               4 B
// 320   uint64_t       sequence          8 B (per-sink, advances per attempt)
// 328   uint64_t       publish_ts_ns     8 B (0 = clock unavailable)
// 336   uint16_t       version           2 B (format version)
// 338   uint8_t        reserved[46]     46 B (future session/category/dictionary)
// 384   → sizeof == 384 == 6 cache lines, alignas(64), trivially copyable
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

/// @brief The fixed, pointer-free span record published over iceoryx2.
///
/// The cross-process mirror of `SpanRecord`: hashes replace the two
/// program-static source pointers, and publisher identity
/// (application code, pid, sequence, publish timestamp) plus the wire format
/// version travel inside the frame. Trivially copyable, 384 bytes and
/// `alignas(64)`, so a publisher can write it in place into a shared-memory slot
/// and a receiver can read the same bytes in another process with no pointer
/// fix-up.
///
/// @note Spec `tracing/iceoryx2-trace-sink`, requirement "SpanFrame is a fixed
///       pointer-free wire record".
struct alignas(64) SpanFrame {
    /// @brief Wire format version carried by every frame.
    ///
    /// A receiver rejects unknown versions instead of misinterpreting the
    /// layout; the value is pinned alongside the offsets below.
    static constexpr std::uint16_t format_version = 1;

    /// @brief Inline span name capacity in bytes (≤ 47 chars + NUL).
    static constexpr std::size_t name_capacity = 47;

    TraceId trace_id{};                        ///< causal group
    SpanId span_id{};                          ///< this span
    SpanId parent_span_id{};                   ///< zero = root
    SpanKind kind{SpanKind::Internal};         ///< causal role
    SpanStatus status{SpanStatus::Unset};      ///< default Unset
    std::uint8_t flags{};                      ///< SpanRecord flag bits
    std::uint8_t attribute_count{};            ///< ≤ MaxAttributes
    std::uint32_t thread_id{};                 ///< producer SYS_gettid
    std::uint64_t start_ns{};                  ///< monotonic span start
    std::uint64_t end_ns{};                    ///< monotonic span end
    std::uint64_t file_hash{};                 ///< FNV-1a of the source file
    std::uint64_t function_hash{};             ///< FNV-1a of the function name
    std::uint32_t line{};                      ///< source line
    std::uint32_t column{};                    ///< source column
    char name[name_capacity + 1]{};            ///< inline name + NUL
    SpanAttribute attributes[MaxAttributes]{}; ///< inline attribute array
    char app_id[4]{};                          ///< 4-byte ASCII application code
    std::uint32_t pid{};                       ///< publisher process id
    std::uint64_t sequence{};                  ///< per-sink monotonic sequence
    std::uint64_t publish_ts_ns{};             ///< 0 = clock unavailable
    std::uint16_t version{format_version};     ///< wire format version
    std::uint8_t reserved[46]{};               ///< forward-compatible tail

    /// @brief The span name as a bounded view.
    ///
    /// @return View over `name`, without the NUL; never allocates.
    [[nodiscard]] std::string_view name_view() const noexcept {
        return std::string_view{name, std::char_traits<char>::length(name)};
    }

    /// @brief The application code as a non-NUL-terminated view (trimmed at NUL).
    ///
    /// @return Up to four ASCII bytes, stopping at the first NUL; empty when
    ///         the field is all zeros.
    [[nodiscard]] std::string_view app_id_text() const noexcept {
        std::size_t n = 0U;
        while (n < sizeof(app_id) && app_id[n] != '\0') {
            ++n;
        }
        return std::string_view{app_id, n};
    }

    /// @brief The end-minus-start duration in nanoseconds (0 for a malformed pair).
    ///
    /// @return Elapsed nanoseconds, or `0` when `end_ns < start_ns`.
    [[nodiscard]] std::uint64_t duration_ns() const noexcept {
        return end_ns >= start_ns ? end_ns - start_ns : 0ULL;
    }

    /// @brief Cold-path diagnostic (mandate 9).
    ///
    /// @return Text naming the identifiers, span name, application code and
    ///         sequence.
    /// @note Allocates; never reached from the publishing worker's hot loop.
    ///       Body in `span_frame_format.cpp`.
    [[nodiscard]] std::string to_string() const;
};

static_assert(sizeof(SpanFrame) == 384, "SpanFrame must stay 384 B = 6 cache lines");
static_assert(alignof(SpanFrame) == 64, "SpanFrame must stay cache-line aligned");
static_assert(std::is_trivially_copyable_v<SpanFrame>, "SpanFrame must cross shared memory");
static_assert(offsetof(SpanFrame, trace_id) == 0);
static_assert(offsetof(SpanFrame, span_id) == 8);
static_assert(offsetof(SpanFrame, parent_span_id) == 16);
static_assert(offsetof(SpanFrame, kind) == 24);
static_assert(offsetof(SpanFrame, status) == 25);
static_assert(offsetof(SpanFrame, flags) == 26);
static_assert(offsetof(SpanFrame, attribute_count) == 27);
static_assert(offsetof(SpanFrame, thread_id) == 28);
static_assert(offsetof(SpanFrame, start_ns) == 32);
static_assert(offsetof(SpanFrame, end_ns) == 40);
static_assert(offsetof(SpanFrame, file_hash) == 48);
static_assert(offsetof(SpanFrame, function_hash) == 56);
static_assert(offsetof(SpanFrame, line) == 64);
static_assert(offsetof(SpanFrame, column) == 68);
static_assert(offsetof(SpanFrame, name) == 72);
static_assert(offsetof(SpanFrame, attributes) == 120);
static_assert(offsetof(SpanFrame, app_id) == 312);
static_assert(offsetof(SpanFrame, pid) == 316);
static_assert(offsetof(SpanFrame, sequence) == 320);
static_assert(offsetof(SpanFrame, publish_ts_ns) == 328);
static_assert(offsetof(SpanFrame, version) == 336);
static_assert(offsetof(SpanFrame, reserved) == 338);

/// @brief Free helper (mandate 9): mirrors `SpanFrame::to_string`.
///
/// @param frame Frame to render.
/// @return The same text as the member.
[[nodiscard]] inline std::string to_string(const SpanFrame& frame) {
    return frame.to_string();
}

/// @brief ADL JSON writer for a frame (cold path, full field set).
///
/// @param j Destination document or field to overwrite.
/// @param frame Frame to serialize; hashes and publisher identity included.
/// @note Spec `tracing/iceoryx2-trace-sink`, requirement "SpanFrame is a fixed
///       pointer-free wire record". Body in `span_frame_format.cpp`.
void to_json(nlohmann::json& j, const SpanFrame& frame);

/// @brief Non-throwing JSON reader for a frame (full round-trip).
///
/// @param j JSON value to read; malformed or wrong-typed fields are ignored.
/// @param out Destination; only recognized fields are updated.
/// @note Spec `tracing/iceoryx2-trace-sink`, requirement "SpanFrame is a fixed
///       pointer-free wire record". Body in `span_frame_format.cpp`.
void from_json(const nlohmann::json& j, SpanFrame& out) noexcept;

} // namespace nexigen::tracing

/// @brief `std::formatter` for `SpanFrame` (mandate 11).
///
/// Renders exactly `SpanFrame::to_string()` through the `std::string_view`
/// formatter, so fill, alignment and width options still apply.
template <> struct std::formatter<nexigen::tracing::SpanFrame> : std::formatter<std::string_view> {
    /// @brief Render one frame as its diagnostic text.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param frame Frame to render.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::tracing::SpanFrame& frame, Ctx& ctx) const {
        const std::string text = frame.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
