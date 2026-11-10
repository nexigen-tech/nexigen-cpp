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

/// Maximum length of the name buffers in an audit record.
inline constexpr std::size_t k_audit_name_length = 32;

/// One journaled transition event.
struct TransitionRecord {
    std::uint64_t timestamp_ns{0};
    char function_group[k_audit_name_length]{};
    char old_state[k_audit_name_length]{};
    char new_state[k_audit_name_length]{};
    char requester_id[k_audit_name_length]{};
    TransitionResult result{TransitionResult::Success};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format(
            "TransitionRecord(timestamp_ns={}, function_group={}, old_state={}, new_state={}, "
            "requester_id={}, result={})",
            timestamp_ns, std::string(function_group), std::string(old_state),
            std::string(new_state), std::string(requester_id),
            nexigen::sm::to_string(result));
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const TransitionRecord& record) {
    return record.to_string();
}

} // namespace nexigen::sm

/// The record formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::sm::TransitionRecord> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::sm::TransitionRecord& record, Ctx& ctx) const {
        const std::string text = record.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
