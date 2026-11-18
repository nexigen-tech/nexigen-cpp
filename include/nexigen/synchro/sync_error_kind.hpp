// nexigen::synchro — error classification vocabulary
// (change add-synchro-event-sync; spec synchro/protocol).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::synchro {

/// @brief Area that produced a synchronization failure.
///
/// The kind selects which cause field of `SyncError` is meaningful.
///
/// @note Spec `synchro/protocol`, requirement "Synchronization error model".
enum class SyncErrorKind : std::uint8_t {
    InvalidConfig, ///< Configuration or start policy rejected before transport.
    Transport,     ///< A `com` channel, producer, consumer, or set operation failed.
    Metrics,       ///< A metric instrument could not be registered.
    Clock,         ///< A raw-monotonic clock read failed.
    EventFd,       ///< The wake eventfd could not be created, polled, or drained.
    Thread,        ///< The real-time worker could not be started.
    Lifecycle,     ///< The requested operation is invalid in the current state.
};

/// @brief Canonical name of an error kind.
/// @param value Kind to render.
/// @return Static-storage canonical spelling. Never allocates.
/// @note Spec `synchro/protocol`, requirement "Synchronization error model";
///       mandate 9.
[[nodiscard]] constexpr std::string_view to_string(SyncErrorKind value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical error-kind name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
/// @note Spec `synchro/protocol`, requirement "Synchronization error model".
[[nodiscard]] constexpr bool from_string(std::string_view name, SyncErrorKind& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for an error kind.
/// @param j Destination JSON value.
/// @param value Kind to serialize.
/// @note Mandate 10; cold path only.
inline void to_json(nlohmann::json& j, const SyncErrorKind value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for an error kind, strictly non-throwing.
/// @param j Source JSON value.
/// @param value Destination, unchanged on failure.
/// @note Mandate 10; cold path only.
inline void from_json(const nlohmann::json& j, SyncErrorKind& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::synchro

namespace nexigen::core {

/// @brief Vocabulary table binding `SyncErrorKind` to canonical names.
template <> struct EnumTraits<nexigen::synchro::SyncErrorKind> {
    /// @brief One value/name pair.
    struct Entry {
        nexigen::synchro::SyncErrorKind value; ///< Enumerator.
        std::string_view name;                 ///< Canonical name.
    };
    /// @brief Canonical table in declaration order.
    static constexpr std::array<Entry, 7> entries{{
        {nexigen::synchro::SyncErrorKind::InvalidConfig, "InvalidConfig"},
        {nexigen::synchro::SyncErrorKind::Transport, "Transport"},
        {nexigen::synchro::SyncErrorKind::Metrics, "Metrics"},
        {nexigen::synchro::SyncErrorKind::Clock, "Clock"},
        {nexigen::synchro::SyncErrorKind::EventFd, "EventFd"},
        {nexigen::synchro::SyncErrorKind::Thread, "Thread"},
        {nexigen::synchro::SyncErrorKind::Lifecycle, "Lifecycle"},
    }};
};

} // namespace nexigen::core
