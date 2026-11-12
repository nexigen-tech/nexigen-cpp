// nexigen::sm — the Function Group state payload (change add-sm; extracted in
// add-app-phm-client so the app-side PHM client can read the published state
// without pulling the whole StatePublication type).
//
// Fixed-size, trivially copyable: the Field pattern payload every application
// reads through the shared-memory state service.
#pragma once

#include <cstddef>
#include <format>
#include <string>

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::sm {

/// @brief Maximum length of a published group-state string.
///
/// The buffer includes its terminating NUL, so a state name is at most 31
/// bytes. Longer names are truncated by the publisher rather than rejected.
inline constexpr std::size_t k_group_state_length = 32;

/// @brief The published payload of one Function Group's state.
///
/// A plain fixed `char` buffer with no dynamic members, which is what the com
/// `Field` pattern requires: the value is copied as bytes into shared memory
/// with `history_size = 1`, so a process starting after a state change reads
/// the current state immediately without sending a request. The type is
/// trivially copyable and safe for zero-copy transport.
///
/// @note Spec `sm/ipc`, requirement "Current state publication".
struct GroupStatePayload {
    char state[k_group_state_length]{}; ///< NUL-terminated state name, zero-filled.

    /// @brief Render the payload as a diagnostic string.
    ///
    /// @return Text naming the published state; allocates through `std::string`
    ///         and `std::format`.
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("GroupStatePayload(state={})", std::string(state));
    }
};

/// @brief Compile-time proof that the payload satisfies the `Message` concept.
///
/// The concept requires plain bytes and trivially copyable storage, so the
/// payload can travel zero-copy over the shared-memory transport.
static_assert(com::concepts::Message<GroupStatePayload>);

/// @brief Free diagnostic helper mirroring `GroupStatePayload::to_string`.
///
/// @param payload Payload to render.
/// @return The member `to_string()` output.
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const GroupStatePayload& payload) {
    return payload.to_string();
}

} // namespace nexigen::sm
