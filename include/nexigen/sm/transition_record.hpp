// nexigen::sm — transition audit record (change add-sm; spec sm/telemetry
// "Transition audit log").
//
// One journaled state-change event: monotonic timestamp, Function Group, old
// and new state, requester id, and the arbitration result. Fixed buffers
// (trivially copyable) so records fit the lock-free ring buffer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

#include <nexigen/sm/transition_result.hpp>

namespace nexigen::sm {

/// @brief Maximum length of the name buffers in an audit record.
///
/// Each buffer includes its terminating NUL, so a stored name is at most 31
/// bytes and is truncated rather than rejected.
inline constexpr std::size_t k_audit_name_length = 32;

/// @brief One journaled transition event.
///
/// Fixed inline buffers and a `std::uint8_t`-backed result make the type
/// trivially copyable, which is what lets the arbitration thread push it into
/// the lock-free SPSC ring in O(1) without allocating.
///
/// @note Spec `sm/telemetry`, requirement "Transition audit log".
struct TransitionRecord {
    std::uint64_t timestamp_ns{0};                      ///< Monotonic event timestamp.
    char function_group[k_audit_name_length]{};         ///< Affected group name.
    char old_state[k_audit_name_length]{};              ///< State before the request.
    char new_state[k_audit_name_length]{};              ///< Requested or applied state.
    char requester_id[k_audit_name_length]{};           ///< Requester identity.
    TransitionResult result{TransitionResult::Success}; ///< Arbitration outcome.

    /// @brief Render the record as a diagnostic string.
    ///
    /// @return Text naming the timestamp, group, states, requester, and
    ///         result; allocates.
    /// @note Spec `sm/telemetry`, requirement "Transition audit log".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format(
            "TransitionRecord(timestamp_ns={}, function_group={}, old_state={}, new_state={}, "
            "requester_id={}, result={})",
            timestamp_ns, std::string(function_group), std::string(old_state),
            std::string(new_state), std::string(requester_id), nexigen::sm::to_string(result));
    }
};

/// @brief Free diagnostic helper mirroring `TransitionRecord::to_string`.
///
/// @param record Record to render.
/// @return The member `to_string()` output.
/// @note Spec `sm/telemetry`, requirement "Transition audit log".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const TransitionRecord& record) {
    return record.to_string();
}

} // namespace nexigen::sm

/// @brief `std::formatter` for `TransitionRecord` (project mandate 11).
///
/// Delegates to `std::formatter<std::string_view>` after rendering the member
/// diagnostic, so `std::format("{}", record)` equals `to_string()`.
///
/// @note Spec `sm/telemetry`, requirement "Transition audit log".
template <>
struct std::formatter<nexigen::sm::TransitionRecord> : std::formatter<std::string_view> {
    /// @brief Format the record as its diagnostic string.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param record Record to render.
    /// @param ctx Output iterator context receiving the text.
    /// @return Iterator past the written characters.
    template <class Ctx> auto format(const nexigen::sm::TransitionRecord& record, Ctx& ctx) const {
        const std::string text = record.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
