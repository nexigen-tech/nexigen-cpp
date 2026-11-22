// nexigen::chrono — application-side time-base client (change
// add-time-base-service; spec chrono/time-base "Client attach and required
// domains", "Holdover and fail-fast").
//
// The client attaches read-only to the daemon's published block, caches the
// rows it declared as required, and refreshes only when the generation moves.
// It owns the failure policy: a daemon restart that preserves every required
// epoch is transparent, while a lost required domain, an epoch change, or a
// heartbeat older than the holdover window ends in `Lost` — the application then
// stops with `time_base_lost` and does not retry on its own.
//
// HOT PATH: `canonical_from_native()` is pure arithmetic over the cached row.
// `refresh()` is one relaxed/acquire generation load in the common case.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/serialization_mandate.hpp>

#include <nexigen/chrono/detail/time_base_segment.hpp>
#include <nexigen/chrono/detail/time_base_seqlock.hpp>
#include <nexigen/chrono/domain_coefficient.hpp>
#include <nexigen/chrono/time_base_block.hpp>
#include <nexigen/chrono/time_base_status.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/concepts/diagnostic_logger_concept.hpp>
#include <nexigen/core/diagnostic_binding.hpp>

namespace nexigen::chrono {

/// @brief Client-side health of the attached time base.
///
/// @note Spec `chrono/time-base`, requirement "Holdover and fail-fast".
enum class ClientState : std::uint8_t {
    Attached, ///< Heartbeat fresh and every required domain present.
    Holdover, ///< The base is stale but still inside the holdover window.
    Lost,     ///< The base must not be used; the application stops.
};

/// @brief Canonical name of a client state.
///
/// @param value State to name.
/// @return `"attached"`, `"holdover"`, `"lost"`, or `"Unknown"`.
/// @note Spec `chrono/time-base`, requirement "Holdover and fail-fast".
[[nodiscard]] constexpr std::string_view to_string(ClientState value) noexcept {
    switch (value) {
        case ClientState::Attached: return "attached";
        case ClientState::Holdover: return "holdover";
        case ClientState::Lost:     return "lost";
    }
    return "Unknown";
}

/// @brief Parse a client-state name.
///
/// @param name Candidate name.
/// @param out Destination, untouched on a miss.
/// @return `true` when the name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, ClientState& out) noexcept {
    if (name == "attached") {
        out = ClientState::Attached;
        return true;
    }
    if (name == "holdover") {
        out = ClientState::Holdover;
        return true;
    }
    if (name == "lost") {
        out = ClientState::Lost;
        return true;
    }
    return false;
}

/// @brief ADL JSON writer for `ClientState` (cold path only).
///
/// @param j Destination JSON value.
/// @param value State to serialize.
/// @note Project mandate 10.
inline void to_json(nlohmann::json& j, const ClientState value) {
    j = std::string(to_string(value));
}

/// @brief ADL JSON reader for `ClientState`, strictly non-throwing.
///
/// @param j Source JSON value.
/// @param out Destination, untouched unless a known name parses.
/// @note Project mandate 10.
inline void from_json(const nlohmann::json& j, ClientState& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

/// @brief Time-base client configuration.
///
/// `required_domains` lists the domain ids the application needs beyond the
/// canonical row (for example `k_host_counter_domain_id`); an empty list needs only the
/// canonical scale. `stale_after` is when the heartbeat stops being fresh and
/// `holdover` is when it becomes lost.
///
/// @note Spec `chrono/time-base`, requirement "Client attach and required
///       domains".
struct TimeBaseClientConfig {
    std::array<std::uint16_t, 8> required_domains{}; ///< Required domain ids.
    std::uint8_t required_count{0};                  ///< Valid entries in `required_domains`.
    std::chrono::nanoseconds stale_after{std::chrono::seconds{1}}; ///< Freshness bound.
    std::chrono::nanoseconds holdover{std::chrono::seconds{2}};    ///< Loss bound.
};

/// @brief A value in one time domain, including its epoch.
struct DomainInstant {
    std::uint16_t domain_id{0}; ///< Domain identifier.
    std::uint64_t epoch{0};     ///< Domain epoch; zero means "use the row epoch".
    std::uint64_t value{0};     ///< Value in the domain's native units.
};

/// @brief Read-only client over one published time-base block.
///
/// Move-only; owns the read-only mapping. Every method is safe to call from one
/// application thread; the hot conversion reads the cached row without a lock.
///
/// @note Spec `chrono/time-base`, requirement "Client attach and required
///       domains".
class TimeBaseClient {
public:
    /// @brief Attach to a published segment and validate the required domains.
    ///
    /// @param segment_name POSIX shared-memory name of the daemon segment.
    /// @param config Required domains and failure windows.
    /// @param binding Optional diagnostics consumer; an attach failure is
    ///        reported as one `Degraded` event before the error returns.
    /// @return The attached client, `NotFound` when the segment, a required
    ///         domain, or a `Lost` base is missing, `NotInitialized` when the
    ///         block is still `NotReady`, `CorruptData` for an incompatible
    ///         block, or `SystemError` when mapping fails.
    /// @note Cold path only (application start).
    [[nodiscard]] static core::Result<TimeBaseClient>
    attach(std::string_view segment_name, const TimeBaseClientConfig& config,
           core::DiagnosticBinding binding = {}) noexcept;

    /// @brief Install or replace the diagnostics consumer.
    ///
    /// Reports the current state once: `Started` when the client is `Attached`,
    /// otherwise `Degraded` with the state's error classification. Control
    /// thread only; the client has no worker.
    ///
    /// @param binding Borrowed consumer; an empty binding disables reporting.
    /// @note Spec `chrono/time-base`, requirement "Time-base diagnostics
    ///       binding".
    void bind_diagnostics(core::DiagnosticBinding binding) noexcept;

    /// @brief Install a typed diagnostics consumer.
    ///
    /// @tparam L Consumer satisfying `core::concepts::DiagnosticLogger`.
    /// @param logger Borrowed consumer; must outlive the client.
    /// @note Spec `chrono/time-base`, requirement "Time-base diagnostics
    ///       binding".
    template <core::concepts::DiagnosticLogger L> void bind_diagnostics(L& logger) noexcept {
        bind_diagnostics(core::DiagnosticBinding{
            &logger, [](void* self, const core::DiagnosticEvent& event) noexcept {
                static_cast<L*>(self)->diagnostic(event);
            }});
    }

    /// @brief Copying is deleted: the client owns a mapping.
    TimeBaseClient(const TimeBaseClient&) = delete;
    /// @brief Copy assignment is deleted: the client owns a mapping.
    TimeBaseClient& operator=(const TimeBaseClient&) = delete;
    /// @brief Move-construct, transferring the mapping.
    TimeBaseClient(TimeBaseClient&&) noexcept = default;
    /// @brief Move-assign, transferring the mapping.
    /// @return This client; the source is left empty.
    /// @note Spec `chrono/time-base`, requirement "Client attach and required
    ///       domains".
    TimeBaseClient& operator=(TimeBaseClient&&) noexcept = default;
    /// @brief Release the mapping.
    ~TimeBaseClient() = default;

    /// @brief Re-read the block when its generation moved.
    ///
    /// @return `true` when a complete block is available (possibly the cached
    ///         one); `false` when the writer held the generation odd for the
    ///         whole retry budget or no block was ever read.
    /// @note Hot path safe: one generation load in the common case.
    [[nodiscard]] bool refresh() noexcept;

    /// @brief Evaluate freshness and required domains at `now_ns`.
    ///
    /// Applies the state machine: fresh → `Attached`; stale inside the holdover
    /// window → `Holdover`; beyond it, or a required domain lost or its epoch
    /// changed → `Lost`.
    ///
    /// @param now_ns Canonical instant of the application tick.
    /// @return The resulting client state.
    /// @note Hot path safe: one refresh plus comparisons.
    [[nodiscard]] ClientState on_tick(std::uint64_t now_ns) noexcept;

    /// @brief Last evaluated client state.
    ///
    /// @return The state resulting from the last `on_tick()`.
    [[nodiscard]] ClientState state() const noexcept { return state_; }

    /// @brief Last published aggregate status.
    ///
    /// @return The block status, or `NotReady` before the first read.
    [[nodiscard]] TimeBaseStatus status() const noexcept { return block_.status; }

    /// @brief Cached row for a domain.
    ///
    /// @param domain_id Domain to look up.
    /// @return Pointer into the cached block, or `nullptr` when absent.
    /// @note Hot path safe.
    [[nodiscard]] const DomainCoefficient* coefficient(std::uint16_t domain_id) const noexcept {
        return block_.find(domain_id);
    }

    /// @brief Convert a native domain value into canonical nanoseconds.
    ///
    /// @param domain_id Domain of `native`.
    /// @param native Value in the domain's own units.
    /// @return Canonical nanoseconds, or `std::nullopt` when the domain is
    ///         absent or no longer usable.
    /// @note Hot path safe.
    [[nodiscard]] std::optional<std::int64_t>
    canonical_from_native(std::uint16_t domain_id, std::uint64_t native) const noexcept;

    /// @brief Convert a canonical instant to UTC through the published series.
    ///
    /// @param canonical_ns Canonical nanoseconds.
    /// @return UTC nanoseconds, or `std::nullopt` when no calibration covers the
    ///         instant.
    /// @note Hot path safe.
    [[nodiscard]] std::optional<std::int64_t>
    utc_from_canonical(std::int64_t canonical_ns) const noexcept;

    /// @brief Convert any domain instant into another domain.
    ///
    /// Maps through `TimeDomain::MonotonicRaw` and the destination reverse map.
    ///
    /// @param from Source domain, epoch and value.
    /// @param to_domain Destination domain id.
    /// @return The converted instant, or `std::nullopt` when a row is absent,
    ///         the epoch differs, or the shared hub interval is not covered.
    /// @note Hot path safe.
    [[nodiscard]] std::optional<DomainInstant> convert(DomainInstant from,
                                                       std::uint16_t to_domain) const noexcept;

    /// @brief Convert an instant into a specific destination epoch.
    ///
    /// @param from Source domain, epoch and value.
    /// @param to_domain Destination domain id.
    /// @param to_epoch Required destination epoch; zero accepts the row epoch.
    /// @return The converted instant, or `std::nullopt` when unresolved.
    [[nodiscard]] std::optional<DomainInstant> convert(DomainInstant from, std::uint16_t to_domain,
                                                       std::uint64_t to_epoch) const noexcept;

    /// @brief The last published 1 Hz snapshot.
    ///
    /// @return A copy of the cached snapshot, or `std::nullopt` when no block
    ///         has been read.
    [[nodiscard]] std::optional<TimeSnapshot> last_snapshot() const noexcept;

    /// @brief The active synchronization mode.
    ///
    /// @return Mode from the cached block.
    [[nodiscard]] SyncMode sync_mode() const noexcept { return block_.sync_mode; }

    /// @brief The active service flags.
    ///
    /// @return Flags from the cached block.
    [[nodiscard]] ServiceFlags service_flags() const noexcept { return block_.service_flags; }

    /// @brief The generation of the cached block.
    ///
    /// @return Last observed generation.
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

    /// @brief The cached block, for diagnostics.
    ///
    /// @return Borrowed view of the cached copy.
    [[nodiscard]] const TimeBaseBlock& block() const noexcept { return block_; }

    /// @brief Render the client as a diagnostic string.
    ///
    /// @return Text naming the state, generation, status and required count.
    /// @note Project mandate 9; allocates.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Adopt an attached segment and configuration.
    ///
    /// @param segment Read-only mapping.
    /// @param config Copied configuration.
    TimeBaseClient(detail::TimeBaseSegment segment, const TimeBaseClientConfig& config) noexcept;

    /// @brief Whether `domain_id` is in the required list.
    ///
    /// @param domain_id Domain to test.
    /// @return `true` when required.
    [[nodiscard]] bool is_required(std::uint16_t domain_id) const noexcept;

    detail::TimeBaseSegment segment_;       ///< Read-only mapping (owns it).
    detail::TimeBaseReader reader_;         ///< Seqlock reader over the mapping.
    TimeBaseClientConfig config_{};         ///< Copied configuration.
    TimeBaseBlock block_{};                 ///< Cached complete block.
    std::array<std::uint64_t, 8> epochs_{}; ///< Epoch captured for each required domain.
    std::uint64_t generation_{0};           ///< Cached generation.
    bool block_valid_{false};               ///< A complete block was read.
    bool epoch_changed_{false};             ///< A required domain changed epoch.
    bool required_missing_{false};          ///< A required domain disappeared.
    ClientState state_{ClientState::Lost};  ///< Last evaluated state.

    /// @brief Emit the current state through the binding when it changed.
    ///
    /// @param state State just evaluated by `on_tick` or installed by `bind`.
    /// @note Cold/control path only; conversion functions never call it.
    void report_state(ClientState state) noexcept;

    core::DiagnosticBinding diag_{};                     ///< Borrowed diagnostics consumer.
    ClientState last_reported_state_{ClientState::Lost}; ///< Last emitted state.
    bool have_reported_{false};                          ///< At least one event emitted.
};

/// @brief Free-helper mirror of `TimeBaseClient::to_string`.
///
/// @param client Client to render.
/// @return The same text as the member.
/// @note Project mandate 9.
[[nodiscard]] std::string to_string(const TimeBaseClient& client);

} // namespace nexigen::chrono
